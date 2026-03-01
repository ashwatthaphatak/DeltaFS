#include "deltafs/snapshot/snapshot_manager.h"

#include <fstream>
#include <iomanip>
#include <sstream>

#include "deltafs/common/utils.h"

namespace deltafs {
namespace snapshot {

SnapshotManager::SnapshotManager(std::string snapshots_dir)
    : snapshots_dir_(std::move(snapshots_dir)), snapshots_path_(snapshots_dir_ + "/snapshots.meta") {}

common::Status SnapshotManager::Initialize() {
  std::string error;
  if (!common::EnsureDirectory(snapshots_dir_, &error)) {
    return common::Status::Internal("failed to create snapshots dir: " + error);
  }

  std::lock_guard<std::mutex> lock(mutex_);
  snapshots_.clear();
  next_snapshot_seq_ = 1;

  std::ifstream in(snapshots_path_);
  if (!in) {
    return common::Status::Ok();
  }

  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) {
      continue;
    }

    std::istringstream iss(line);
    SnapshotRecord record;
    if (!(iss >> std::quoted(record.snapshot_id) >> std::quoted(record.name) >> record.root_version >>
          record.created_at_unix_ms)) {
      continue;
    }

    snapshots_.push_back(record);
    if (record.snapshot_id.rfind("snap-", 0) == 0) {
      try {
        const auto seq = std::stoull(record.snapshot_id.substr(5));
        if (seq >= next_snapshot_seq_) {
          next_snapshot_seq_ = seq + 1;
        }
      } catch (...) {
      }
    }
  }

  return common::Status::Ok();
}

common::Status SnapshotManager::PersistAllLocked() const {
  std::ofstream out(snapshots_path_, std::ios::trunc);
  if (!out) {
    return common::Status::Internal("failed to write snapshots metadata");
  }

  for (const auto& snapshot : snapshots_) {
    out << std::quoted(snapshot.snapshot_id) << ' ' << std::quoted(snapshot.name) << ' '
        << snapshot.root_version << ' ' << snapshot.created_at_unix_ms << '\n';
  }

  out.flush();
  if (!out) {
    return common::Status::Internal("failed to flush snapshots metadata");
  }

  return common::Status::Ok();
}

common::Status SnapshotManager::CreateSnapshot(const std::string& name,
                                               uint64_t root_version,
                                               std::string* snapshot_id,
                                               int64_t* created_at_unix_ms) {
  std::lock_guard<std::mutex> lock(mutex_);

  SnapshotRecord record;
  record.snapshot_id = "snap-" + std::to_string(next_snapshot_seq_++);
  record.name = name;
  record.root_version = root_version;
  record.created_at_unix_ms = common::NowUnixMillis();

  snapshots_.push_back(record);
  auto status = PersistAllLocked();
  if (!status.ok()) {
    snapshots_.pop_back();
    return status;
  }

  *snapshot_id = record.snapshot_id;
  *created_at_unix_ms = record.created_at_unix_ms;
  return common::Status::Ok();
}

common::Status SnapshotManager::ApplyReplicatedSnapshot(const std::string& snapshot_id,
                                                        const std::string& name,
                                                        uint64_t root_version,
                                                        int64_t created_at_unix_ms) {
  std::lock_guard<std::mutex> lock(mutex_);

  for (const auto& existing : snapshots_) {
    if (existing.snapshot_id == snapshot_id) {
      if (existing.root_version == root_version) {
        return common::Status::Ok();
      }
      return common::Status::FailedPrecondition("snapshot id conflict: " + snapshot_id);
    }
  }

  SnapshotRecord record;
  record.snapshot_id = snapshot_id;
  record.name = name;
  record.root_version = root_version;
  record.created_at_unix_ms = created_at_unix_ms;
  snapshots_.push_back(record);

  if (snapshot_id.rfind("snap-", 0) == 0) {
    try {
      const auto seq = std::stoull(snapshot_id.substr(5));
      if (seq >= next_snapshot_seq_) {
        next_snapshot_seq_ = seq + 1;
      }
    } catch (...) {
    }
  }

  return PersistAllLocked();
}

common::Status SnapshotManager::GetSnapshotRoot(const std::string& snapshot_id,
                                                uint64_t* root_version) const {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto& snapshot : snapshots_) {
    if (snapshot.snapshot_id == snapshot_id) {
      *root_version = snapshot.root_version;
      return common::Status::Ok();
    }
  }
  return common::Status::NotFound("snapshot not found: " + snapshot_id);
}

std::vector<SnapshotRecord> SnapshotManager::ListSnapshots() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return snapshots_;
}

}  // namespace snapshot
}  // namespace deltafs
