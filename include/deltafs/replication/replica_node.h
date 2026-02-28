#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "deltafs/blockstore/block_store.h"
#include "deltafs/common/config.h"
#include "deltafs/common/dedupe_store.h"
#include "deltafs/common/status.h"
#include "deltafs/journal/journal_manager.h"
#include "deltafs/membership/membership_provider.h"
#include "deltafs/metadata/metadata_manager.h"
#include "deltafs/replication/replication_manager.h"
#include "deltafs/snapshot/snapshot_manager.h"
#include "common.pb.h"
#include "replication.pb.h"

namespace deltafs {
namespace replication {

struct PutKeyResult {
  bool applied = false;
  uint64_t lsn = 0;
  uint64_t root_version = 0;
  std::string block_id;
};

struct GetKeyResult {
  bool found = false;
  std::string block_id;
  std::string value;
  uint64_t root_version = 0;
};

struct CreateSnapshotResult {
  std::string snapshot_id;
  uint64_t root_version = 0;
  uint64_t lsn = 0;
};

struct AdminStatus {
  std::string node_id;
  std::string role;
  std::string leader_id;
  uint64_t commit_index = 0;
  uint64_t last_applied = 0;
  uint64_t committed_root = 0;
  uint64_t snapshots_count = 0;
  std::vector<common::PeerEndpoint> peers;
};

class ReplicaNode {
 public:
  explicit ReplicaNode(common::NodeConfig config);

  common::Status Initialize();

  const common::NodeConfig& config() const { return config_; }

  common::Status PutKey(const deltafs::v1::RequestContext& context,
                        const std::string& key,
                        const std::string& value,
                        uint64_t root_version_context,
                        PutKeyResult* result);

  common::Status GetKey(const std::string& key,
                        const std::string& snapshot_id,
                        uint64_t root_version,
                        GetKeyResult* result);

  common::Status CreateSnapshot(const deltafs::v1::RequestContext& context,
                                const std::string& name,
                                CreateSnapshotResult* result);

  common::Status ReadAtSnapshot(const std::string& snapshot_id,
                                const std::string& key,
                                GetKeyResult* result);

  std::vector<snapshot::SnapshotRecord> ListSnapshots() const;

  common::Status PutBlockRaw(const deltafs::v1::RequestContext& context,
                             const std::string& data,
                             std::string* block_id);

  common::Status GetBlockRaw(const std::string& block_id, std::string* data, bool* found) const;

  common::Status JournalAppend(const deltafs::v1::RequestContext& context,
                               const deltafs::v1::WalEntry& entry,
                               uint64_t* lsn);

  common::Status HandleAppendEntries(const deltafs::v1::AppendEntriesRequest& request,
                                     deltafs::v1::AppendEntriesResponse* response);

  common::Status GetAdminStatus(AdminStatus* status) const;

  common::Status ForceConsistencyPoint(bool* forced,
                                       uint64_t* commit_index,
                                       uint64_t* committed_root);

  common::Status PromoteToLeader(bool force, bool* promoted, std::string* role);

  uint64_t JournalLastLsn() const;
  common::Status JournalLastEntry(deltafs::v1::WalEntry* entry) const;

 private:
  common::Status ApplyWalEntry(const deltafs::v1::WalEntry& entry);
  common::Status RecordDedupe(const common::DedupeRecord& record);

  common::NodeConfig config_;

  blockstore::BlockStore block_store_;
  journal::JournalManager journal_;
  metadata::MetadataManager metadata_;
  snapshot::SnapshotManager snapshot_manager_;
  common::DedupeStore dedupe_store_;

  std::unique_ptr<membership::MembershipProvider> membership_provider_;
  std::unique_ptr<ReplicationManager> replication_manager_;

  mutable std::mutex write_mutex_;
};

}  // namespace replication
}  // namespace deltafs
