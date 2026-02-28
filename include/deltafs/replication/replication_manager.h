#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <grpcpp/grpcpp.h>

#include "deltafs/common/config.h"
#include "deltafs/common/status.h"
#include "deltafs/journal/journal_manager.h"
#include "deltafs/membership/membership_provider.h"
#include "replication.grpc.pb.h"

namespace deltafs {
namespace replication {

class ReplicationManager {
 public:
  struct Callbacks {
    std::function<common::Status(const deltafs::v1::WalEntry&)> apply_entry;
    std::function<uint64_t()> current_committed_root;
  };

  ReplicationManager(common::NodeConfig config,
                     membership::MembershipProvider* membership_provider,
                     journal::JournalManager* journal,
                     Callbacks callbacks);

  common::Status Initialize();

  bool is_leader() const;
  std::string role() const;
  std::string leader_id() const;

  uint64_t commit_index() const;
  uint64_t last_applied() const;
  uint64_t committed_root() const;

  std::vector<common::PeerEndpoint> peers() const;

  common::Status ReplicateClientEntry(const deltafs::v1::WalEntry& entry_template,
                                      deltafs::v1::WalEntry* committed_entry);

  common::Status ForceConsistencyPoint(bool* forced);

  common::Status HandleAppendEntries(const deltafs::v1::AppendEntriesRequest& request,
                                     deltafs::v1::AppendEntriesResponse* response);

  common::Status PromoteToLeader(bool force, bool* promoted);

 private:
  common::Status LoadStateLocked();
  common::Status PersistStateLocked();
  common::Status ApplyCommittedUpToLocked(uint64_t target_commit_index);

  common::Status ReplicateToPeerLocked(const common::PeerEndpoint& peer, uint64_t target_lsn);
  common::Status SendAppendEntriesLocked(const common::PeerEndpoint& peer,
                                         const deltafs::v1::AppendEntriesRequest& request,
                                         deltafs::v1::AppendEntriesResponse* response);

  common::Status BroadcastCommitIndexLocked();

  bool AckSatisfiedLocked(size_t ack_count) const;

  common::NodeConfig config_;
  membership::MembershipProvider* membership_provider_;
  journal::JournalManager* journal_;
  Callbacks callbacks_;

  std::string state_path_;

  mutable std::mutex mutex_;
  std::string leader_id_;
  uint64_t commit_index_ = 0;
  uint64_t last_applied_ = 0;
  uint64_t committed_root_ = 0;

  std::vector<common::PeerEndpoint> peers_;
  std::unordered_map<std::string, uint64_t> follower_match_index_;
  std::unordered_map<std::string, std::unique_ptr<deltafs::v1::ReplicationService::Stub>> stubs_;
};

}  // namespace replication
}  // namespace deltafs
