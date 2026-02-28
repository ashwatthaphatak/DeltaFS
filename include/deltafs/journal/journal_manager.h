#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "deltafs/common/status.h"
#include "replication.pb.h"

namespace deltafs {
namespace journal {

class JournalManager {
 public:
  explicit JournalManager(std::string wal_dir, size_t segment_max_bytes = 4 * 1024 * 1024);
  ~JournalManager();

  common::Status Initialize();

  common::Status AppendNewEntry(const deltafs::v1::WalEntry& entry_template,
                                deltafs::v1::WalEntry* appended_entry);

  common::Status AppendReplicatedEntry(const deltafs::v1::WalEntry& entry);

  common::Status GetEntry(uint64_t lsn, deltafs::v1::WalEntry* out) const;
  std::vector<deltafs::v1::WalEntry> GetEntriesRange(uint64_t start_lsn,
                                                      uint64_t end_lsn,
                                                      size_t max_entries) const;

  uint64_t last_lsn() const;
  size_t entry_count() const;

  const std::string& wal_dir() const { return wal_dir_; }

 private:
  common::Status OpenNewSegmentLocked(uint64_t start_lsn);
  common::Status AppendEntryLocked(const deltafs::v1::WalEntry& entry);
  common::Status ReadAllSegmentsLocked();

  std::string wal_dir_;
  size_t segment_max_bytes_;

  mutable std::mutex mutex_;
  uint64_t last_lsn_ = 0;
  std::unordered_map<uint64_t, deltafs::v1::WalEntry> entries_;

  int active_fd_ = -1;
  uint64_t active_segment_start_lsn_ = 0;
  size_t active_segment_size_ = 0;
};

}  // namespace journal
}  // namespace deltafs
