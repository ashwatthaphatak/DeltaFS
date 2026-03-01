#include "deltafs/journal/journal_manager.h"

#include <fcntl.h>
#include <unistd.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#include <zlib.h>

#include "deltafs/common/utils.h"

namespace deltafs {
namespace journal {

namespace {

struct RecordHeader {
  uint32_t payload_size;
  uint32_t crc32;
};

std::string SegmentPath(const std::string& wal_dir, uint64_t start_lsn) {
  std::ostringstream oss;
  oss << wal_dir << "/segment_" << std::setw(20) << std::setfill('0') << start_lsn << ".wal";
  return oss.str();
}

uint32_t ComputeCrc(const std::string& payload) {
  return crc32(0, reinterpret_cast<const Bytef*>(payload.data()), payload.size());
}

bool ReadFully(int fd, void* out, size_t size) {
  char* ptr = static_cast<char*>(out);
  size_t read_total = 0;
  while (read_total < size) {
    const ssize_t got = read(fd, ptr + read_total, size - read_total);
    if (got == 0) {
      return false;
    }
    if (got < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    read_total += static_cast<size_t>(got);
  }
  return true;
}

bool WriteFully(int fd, const void* data, size_t size) {
  const char* ptr = static_cast<const char*>(data);
  size_t sent = 0;
  while (sent < size) {
    const ssize_t wrote = write(fd, ptr + sent, size - sent);
    if (wrote < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    sent += static_cast<size_t>(wrote);
  }
  return true;
}

}  // namespace

JournalManager::JournalManager(std::string wal_dir, size_t segment_max_bytes)
    : wal_dir_(std::move(wal_dir)), segment_max_bytes_(segment_max_bytes) {}

JournalManager::~JournalManager() {
  if (active_fd_ >= 0) {
    close(active_fd_);
    active_fd_ = -1;
  }
}

common::Status JournalManager::Initialize() {
  std::string error;
  if (!common::EnsureDirectory(wal_dir_, &error)) {
    return common::Status::Internal("failed to create wal dir: " + error);
  }

  std::lock_guard<std::mutex> lock(mutex_);

  entries_.clear();
  last_lsn_ = 0;

  if (active_fd_ >= 0) {
    close(active_fd_);
    active_fd_ = -1;
  }

  auto status = ReadAllSegmentsLocked();
  if (!status.ok()) {
    return status;
  }

  if (last_lsn_ == 0) {
    return OpenNewSegmentLocked(1);
  }

  const uint64_t next_lsn = last_lsn_ + 1;
  return OpenNewSegmentLocked(next_lsn);
}

common::Status JournalManager::ReadAllSegmentsLocked() {
  for (const auto& path : common::ListFilesSorted(wal_dir_, "segment_", ".wal")) {
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
      return common::Status::Internal("failed to open wal segment: " + path);
    }

    while (true) {
      RecordHeader header;
      const ssize_t got = read(fd, &header, sizeof(header));
      if (got == 0) {
        break;
      }
      if (got < 0) {
        close(fd);
        return common::Status::Internal("failed reading wal segment header");
      }
      if (static_cast<size_t>(got) != sizeof(header)) {
        close(fd);
        return common::Status::Internal("wal segment truncated header");
      }
      if (header.payload_size == 0) {
        close(fd);
        return common::Status::Internal("wal segment has invalid empty record");
      }

      std::string payload;
      payload.resize(header.payload_size);
      if (!ReadFully(fd, payload.data(), payload.size())) {
        close(fd);
        return common::Status::Internal("wal segment truncated payload");
      }

      if (ComputeCrc(payload) != header.crc32) {
        close(fd);
        return common::Status::Internal("wal record crc mismatch");
      }

      deltafs::v1::WalEntry entry;
      if (!entry.ParseFromString(payload)) {
        close(fd);
        return common::Status::Internal("failed to parse wal entry");
      }

      if (entry.lsn() == 0) {
        close(fd);
        return common::Status::Internal("wal entry has lsn=0");
      }

      if (entry.lsn() <= last_lsn_) {
        const auto existing = entries_.find(entry.lsn());
        if (existing == entries_.end()) {
          close(fd);
          return common::Status::Internal("wal lsn gap or duplicate inconsistency");
        }
        continue;
      }

      if (entry.lsn() != last_lsn_ + 1) {
        close(fd);
        return common::Status::Internal("wal lsn sequence broken at " +
                                        std::to_string(entry.lsn()));
      }

      entries_[entry.lsn()] = entry;
      last_lsn_ = entry.lsn();
    }

    close(fd);
  }

  return common::Status::Ok();
}

common::Status JournalManager::OpenNewSegmentLocked(uint64_t start_lsn) {
  if (active_fd_ >= 0) {
    close(active_fd_);
    active_fd_ = -1;
  }

  const std::string path = SegmentPath(wal_dir_, start_lsn);
  active_fd_ = open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
  if (active_fd_ < 0) {
    return common::Status::Internal("failed to open wal segment for append: " + path);
  }

  std::error_code ec;
  active_segment_size_ = std::filesystem::file_size(path, ec);
  if (ec) {
    active_segment_size_ = 0;
  }
  active_segment_start_lsn_ = start_lsn;
  return common::Status::Ok();
}

common::Status JournalManager::AppendEntryLocked(const deltafs::v1::WalEntry& entry) {
  std::string payload;
  if (!entry.SerializeToString(&payload)) {
    return common::Status::Internal("failed serializing wal entry");
  }

  const size_t record_size = sizeof(RecordHeader) + payload.size();

  if (active_fd_ < 0) {
    auto status = OpenNewSegmentLocked(entry.lsn());
    if (!status.ok()) {
      return status;
    }
  }

  if (active_segment_size_ + record_size > segment_max_bytes_) {
    auto status = OpenNewSegmentLocked(entry.lsn());
    if (!status.ok()) {
      return status;
    }
  }

  RecordHeader header;
  header.payload_size = static_cast<uint32_t>(payload.size());
  header.crc32 = ComputeCrc(payload);

  if (!WriteFully(active_fd_, &header, sizeof(header)) ||
      !WriteFully(active_fd_, payload.data(), payload.size())) {
    return common::Status::Internal("failed appending wal entry");
  }

  if (fsync(active_fd_) != 0) {
    return common::Status::Internal("failed fsync wal entry");
  }

  active_segment_size_ += record_size;
  entries_[entry.lsn()] = entry;
  last_lsn_ = entry.lsn();
  return common::Status::Ok();
}

common::Status JournalManager::AppendNewEntry(const deltafs::v1::WalEntry& entry_template,
                                              deltafs::v1::WalEntry* appended_entry) {
  std::lock_guard<std::mutex> lock(mutex_);

  deltafs::v1::WalEntry entry = entry_template;
  entry.set_lsn(last_lsn_ + 1);

  auto status = AppendEntryLocked(entry);
  if (!status.ok()) {
    return status;
  }

  *appended_entry = std::move(entry);
  return common::Status::Ok();
}

common::Status JournalManager::AppendReplicatedEntry(const deltafs::v1::WalEntry& entry) {
  if (entry.lsn() == 0) {
    return common::Status::InvalidArgument("replicated entry requires lsn");
  }

  std::lock_guard<std::mutex> lock(mutex_);

  if (entry.lsn() <= last_lsn_) {
    const auto it = entries_.find(entry.lsn());
    if (it == entries_.end()) {
      return common::Status::Internal("wal index mismatch for duplicate lsn");
    }
    std::string existing_serialized;
    std::string incoming_serialized;
    it->second.SerializeToString(&existing_serialized);
    entry.SerializeToString(&incoming_serialized);
    if (existing_serialized == incoming_serialized) {
      return common::Status::Ok();
    }
    return common::Status::FailedPrecondition("wal duplicate lsn with different payload");
  }

  if (entry.lsn() != last_lsn_ + 1) {
    return common::Status::FailedPrecondition("replicated lsn is not contiguous");
  }

  return AppendEntryLocked(entry);
}

common::Status JournalManager::GetEntry(uint64_t lsn, deltafs::v1::WalEntry* out) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = entries_.find(lsn);
  if (it == entries_.end()) {
    return common::Status::NotFound("wal lsn not found");
  }
  *out = it->second;
  return common::Status::Ok();
}

std::vector<deltafs::v1::WalEntry> JournalManager::GetEntriesRange(uint64_t start_lsn,
                                                                    uint64_t end_lsn,
                                                                    size_t max_entries) const {
  std::lock_guard<std::mutex> lock(mutex_);

  std::vector<deltafs::v1::WalEntry> out;
  if (start_lsn == 0 || start_lsn > last_lsn_) {
    return out;
  }

  const uint64_t capped_end = (end_lsn == 0 || end_lsn > last_lsn_) ? last_lsn_ : end_lsn;
  for (uint64_t lsn = start_lsn; lsn <= capped_end; ++lsn) {
    const auto it = entries_.find(lsn);
    if (it == entries_.end()) {
      break;
    }
    out.push_back(it->second);
    if (max_entries > 0 && out.size() >= max_entries) {
      break;
    }
  }

  return out;
}

uint64_t JournalManager::last_lsn() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return last_lsn_;
}

size_t JournalManager::entry_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return entries_.size();
}

}  // namespace journal
}  // namespace deltafs
