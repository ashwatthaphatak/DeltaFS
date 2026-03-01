#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "deltafs/common/status.h"

namespace deltafs {
namespace snapshot {

struct SnapshotRecord {
  std::string snapshot_id;
  std::string name;
  uint64_t root_version = 0;
  int64_t created_at_unix_ms = 0;
};

class SnapshotManager {
 public:
  explicit SnapshotManager(std::string snapshots_dir);

  common::Status Initialize();
  common::Status CreateSnapshot(const std::string& name,
                                uint64_t root_version,
                                std::string* snapshot_id,
                                int64_t* created_at_unix_ms);

  common::Status ApplyReplicatedSnapshot(const std::string& snapshot_id,
                                         const std::string& name,
                                         uint64_t root_version,
                                         int64_t created_at_unix_ms);

  common::Status GetSnapshotRoot(const std::string& snapshot_id, uint64_t* root_version) const;
  std::vector<SnapshotRecord> ListSnapshots() const;

  const std::string& snapshots_dir() const { return snapshots_dir_; }

 private:
  common::Status PersistAllLocked() const;

  std::string snapshots_dir_;
  std::string snapshots_path_;

  mutable std::mutex mutex_;
  std::vector<SnapshotRecord> snapshots_;
  uint64_t next_snapshot_seq_ = 1;
};

}  // namespace snapshot
}  // namespace deltafs
