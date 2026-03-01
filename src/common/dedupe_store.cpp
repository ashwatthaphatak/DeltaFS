#include "deltafs/common/dedupe_store.h"

#include <fcntl.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#include "deltafs/common/utils.h"

namespace deltafs {
namespace common {

DedupeStore::DedupeStore(std::string dir_path)
    : dir_path_(std::move(dir_path)), records_path_(dir_path_ + "/requests.log") {}

Status DedupeStore::Load() {
  std::string error;
  if (!EnsureDirectory(dir_path_, &error)) {
    return Status::Internal("failed to create dedupe directory: " + error);
  }

  std::lock_guard<std::mutex> lock(mutex_);
  records_.clear();

  std::ifstream in(records_path_);
  if (!in) {
    return Status::Ok();
  }

  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) {
      continue;
    }
    std::istringstream iss(line);
    DedupeRecord rec;
    int success_as_int = 0;
    if (!(iss >> std::quoted(rec.request_id) >> std::quoted(rec.operation) >> success_as_int >> rec.lsn >>
          rec.root_version >> std::quoted(rec.block_id) >> std::quoted(rec.snapshot_id) >>
          rec.timestamp_unix_ms)) {
      continue;
    }
    rec.success = success_as_int != 0;
    if (!rec.request_id.empty()) {
      records_[rec.request_id] = std::move(rec);
    }
  }

  return Status::Ok();
}

bool DedupeStore::Lookup(const std::string& request_id, DedupeRecord* out) const {
  if (request_id.empty()) {
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = records_.find(request_id);
  if (it == records_.end()) {
    return false;
  }
  *out = it->second;
  return true;
}

Status DedupeStore::Remember(const DedupeRecord& record) {
  if (record.request_id.empty()) {
    return Status::InvalidArgument("dedupe record requires request_id");
  }

  const std::string line = [&record]() {
    std::ostringstream out;
    out << std::quoted(record.request_id) << ' ' << std::quoted(record.operation) << ' '
        << (record.success ? 1 : 0) << ' ' << record.lsn << ' ' << record.root_version << ' '
        << std::quoted(record.block_id) << ' ' << std::quoted(record.snapshot_id) << ' '
        << record.timestamp_unix_ms << '\n';
    return out.str();
  }();

  {
    std::lock_guard<std::mutex> lock(mutex_);
    records_[record.request_id] = record;
  }

  int fd = open(records_path_.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
  if (fd < 0) {
    return Status::Internal("failed to open dedupe log: " + records_path_);
  }

  const ssize_t wrote = write(fd, line.data(), line.size());
  if (wrote < 0 || static_cast<size_t>(wrote) != line.size()) {
    close(fd);
    return Status::Internal("failed to append dedupe record");
  }

  if (fsync(fd) != 0) {
    close(fd);
    return Status::Internal("failed to fsync dedupe record");
  }

  close(fd);
  return Status::Ok();
}

size_t DedupeStore::Size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return records_.size();
}

}  // namespace common
}  // namespace deltafs
