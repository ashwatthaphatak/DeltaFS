#include "deltafs/blockstore/block_store.h"

#include <fcntl.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include <zlib.h>

#include "deltafs/common/utils.h"

namespace deltafs {
namespace blockstore {

namespace {

std::string MakeBlockId(const std::string& data, uint64_t counter) {
  const auto crc = crc32(0, reinterpret_cast<const Bytef*>(data.data()), data.size());
  std::ostringstream oss;
  oss << "blk_" << common::NowUnixMillis() << '_' << counter << '_' << std::hex << crc;
  return oss.str();
}

}  // namespace

BlockStore::BlockStore(std::string blocks_dir) : blocks_dir_(std::move(blocks_dir)) {}

common::Status BlockStore::Initialize() {
  std::string error;
  if (!common::EnsureDirectory(blocks_dir_, &error)) {
    return common::Status::Internal("failed to create block directory: " + error);
  }

  uint64_t max_counter = 0;
  for (const auto& path : common::ListFilesSorted(blocks_dir_, "blk_")) {
    const auto filename = std::filesystem::path(path).filename().string();
    const auto parts = common::Split(filename, '_');
    if (parts.size() < 3) {
      continue;
    }
    try {
      const uint64_t counter = std::stoull(parts[2]);
      if (counter > max_counter) {
        max_counter = counter;
      }
    } catch (...) {
    }
  }

  next_counter_ = max_counter + 1;
  return common::Status::Ok();
}

common::Status BlockStore::WriteBlockFile(const std::string& block_id,
                                          const std::string& data,
                                          bool* created) {
  const std::string path = blocks_dir_ + "/" + block_id;

  if (std::filesystem::exists(path)) {
    std::string existing;
    if (!common::ReadFileToString(path, &existing)) {
      return common::Status::Internal("failed to read existing block: " + path);
    }
    if (existing != data) {
      return common::Status::FailedPrecondition("block id collision with different payload");
    }
    *created = false;
    return common::Status::Ok();
  }

  const std::string temp_path = path + ".tmp." + common::GenerateRequestId();
  int fd = open(temp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) {
    return common::Status::Internal("failed to open temp block file: " + temp_path);
  }

  const ssize_t wrote = write(fd, data.data(), data.size());
  if (wrote < 0 || static_cast<size_t>(wrote) != data.size()) {
    close(fd);
    std::filesystem::remove(temp_path);
    return common::Status::Internal("failed to write block: " + block_id);
  }

  if (fsync(fd) != 0) {
    close(fd);
    std::filesystem::remove(temp_path);
    return common::Status::Internal("failed to fsync block file: " + block_id);
  }

  close(fd);

  std::error_code ec;
  std::filesystem::rename(temp_path, path, ec);
  if (ec) {
    std::filesystem::remove(temp_path);
    return common::Status::Internal("failed to finalize block file: " + ec.message());
  }

  *created = true;
  return common::Status::Ok();
}

common::Status BlockStore::PutBlock(const std::string& data, std::string* block_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::string id = MakeBlockId(data, next_counter_++);
  bool created = false;
  auto status = WriteBlockFile(id, data, &created);
  if (!status.ok()) {
    return status;
  }
  *block_id = std::move(id);
  (void)created;
  return common::Status::Ok();
}

common::Status BlockStore::PutBlockWithId(const std::string& block_id, const std::string& data) {
  std::lock_guard<std::mutex> lock(mutex_);
  bool created = false;
  return WriteBlockFile(block_id, data, &created);
}

common::Status BlockStore::GetBlock(const std::string& block_id, std::string* data, bool* found) const {
  const std::string path = blocks_dir_ + "/" + block_id;
  if (!std::filesystem::exists(path)) {
    *found = false;
    data->clear();
    return common::Status::Ok();
  }

  if (!common::ReadFileToString(path, data)) {
    return common::Status::Internal("failed to read block: " + block_id);
  }

  *found = true;
  return common::Status::Ok();
}

}  // namespace blockstore
}  // namespace deltafs
