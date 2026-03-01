#include "deltafs/replication/replication_manager.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>

#include "deltafs/common/grpc_utils.h"
#include "deltafs/common/logging.h"
#include "deltafs/common/utils.h"

namespace deltafs {
namespace replication {

ReplicationManager::ReplicationManager(common::NodeConfig config,
                                       membership::MembershipProvider* membership_provider,
                                       journal::JournalManager* journal,
                                       Callbacks callbacks)
    : config_(std::move(config)),
      membership_provider_(membership_provider),
      journal_(journal),
      callbacks_(std::move(callbacks)),
      state_path_(config_.data_dir + "/metadata/replication_state"),
      leader_id_(config_.leader_id) {}

common::Status ReplicationManager::Initialize() {
  std::lock_guard<std::mutex> lock(mutex_);

  peers_ = membership_provider_->ListPeers();
  follower_match_index_.clear();
  stubs_.clear();

  for (const auto& peer : peers_) {
    if (peer.node_id == config_.node_id) {
      continue;
    }
    follower_match_index_[peer.node_id] = 0;
    auto channel = grpc::CreateChannel(peer.endpoint, grpc::InsecureChannelCredentials());
    stubs_[peer.node_id] = deltafs::v1::ReplicationService::NewStub(channel);
  }

  auto status = LoadStateLocked();
  if (!status.ok()) {
    return status;
  }

  if (commit_index_ > journal_->last_lsn()) {
    return common::Status::FailedPrecondition("replication state commit index exceeds WAL last_lsn");
  }

  status = ApplyCommittedUpToLocked(commit_index_);
  if (!status.ok()) {
    return status;
  }

  committed_root_ = callbacks_.current_committed_root();
  return PersistStateLocked();
}

bool ReplicationManager::is_leader() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return config_.node_id == leader_id_;
}

std::string ReplicationManager::role() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return config_.node_id == leader_id_ ? "leader" : "follower";
}

std::string ReplicationManager::leader_id() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return leader_id_;
}

uint64_t ReplicationManager::commit_index() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return commit_index_;
}

uint64_t ReplicationManager::last_applied() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return last_applied_;
}

uint64_t ReplicationManager::committed_root() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return committed_root_;
}

std::vector<common::PeerEndpoint> ReplicationManager::peers() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return peers_;
}

common::Status ReplicationManager::LoadStateLocked() {
  commit_index_ = 0;
  last_applied_ = 0;
  committed_root_ = callbacks_.current_committed_root();

  std::ifstream in(state_path_);
  if (!in) {
    return common::Status::Ok();
  }

  std::string label;
  while (in >> label) {
    if (label == "leader_id") {
      in >> leader_id_;
    } else if (label == "commit_index") {
      in >> commit_index_;
    } else if (label == "last_applied") {
      in >> last_applied_;
    } else if (label == "committed_root") {
      in >> committed_root_;
    }
  }

  return common::Status::Ok();
}

common::Status ReplicationManager::PersistStateLocked() {
  std::ofstream out(state_path_, std::ios::trunc);
  if (!out) {
    return common::Status::Internal("failed to persist replication state");
  }

  out << "leader_id " << leader_id_ << '\n';
  out << "commit_index " << commit_index_ << '\n';
  out << "last_applied " << last_applied_ << '\n';
  out << "committed_root " << committed_root_ << '\n';

  out.flush();
  if (!out) {
    return common::Status::Internal("failed to flush replication state");
  }

  return common::Status::Ok();
}

common::Status ReplicationManager::ApplyCommittedUpToLocked(uint64_t target_commit_index) {
  while (last_applied_ < target_commit_index) {
    const uint64_t lsn = last_applied_ + 1;
    deltafs::v1::WalEntry entry;
    auto status = journal_->GetEntry(lsn, &entry);
    if (!status.ok()) {
      return status.WithDetail("lsn", std::to_string(lsn));
    }

    status = callbacks_.apply_entry(entry);
    if (!status.ok()) {
      return status.WithDetail("lsn", std::to_string(lsn));
    }

    last_applied_ = lsn;
    committed_root_ = callbacks_.current_committed_root();
  }

  return common::Status::Ok();
}

bool ReplicationManager::AckSatisfiedLocked(size_t ack_count) const {
  const size_t required = common::RequiredAckCount(config_.ack_policy, peers_.size());
  return ack_count >= required;
}

common::Status ReplicationManager::SendAppendEntriesLocked(
    const common::PeerEndpoint& peer,
    const deltafs::v1::AppendEntriesRequest& request,
    deltafs::v1::AppendEntriesResponse* response) {
  auto stub_it = stubs_.find(peer.node_id);
  if (stub_it == stubs_.end()) {
    return common::Status::NotFound("replication stub missing for peer " + peer.node_id);
  }

  common::Status last_error = common::Status::Unavailable("replication call was not attempted");

  for (int attempt = 0; attempt <= config_.max_retries; ++attempt) {
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() +
                     std::chrono::milliseconds(config_.rpc_timeout_ms));

    common::AddContextMetadata(request.context(), &ctx);
    const grpc::Status grpc_status = stub_it->second->AppendEntries(&ctx, request, response);

    if (grpc_status.ok()) {
      return common::Status::Ok();
    }

    last_error = common::FromGrpcStatus(grpc_status)
                     .WithDetail("peer", peer.node_id)
                     .WithDetail("endpoint", peer.endpoint)
                     .WithDetail("attempt", std::to_string(attempt));

    if (!common::IsRetryableGrpcStatus(grpc_status) || attempt == config_.max_retries) {
      return last_error;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50 * (attempt + 1)));
  }

  return last_error;
}

common::Status ReplicationManager::ReplicateToPeerLocked(const common::PeerEndpoint& peer,
                                                         uint64_t target_lsn) {
  uint64_t& match = follower_match_index_[peer.node_id];

  while (match < target_lsn) {
    const uint64_t start_lsn = match + 1;
    auto entries = journal_->GetEntriesRange(start_lsn, target_lsn, 64);
    if (entries.empty()) {
      return common::Status::NotFound("wal entries missing for replication from lsn " +
                                      std::to_string(start_lsn));
    }

    deltafs::v1::AppendEntriesRequest request;
    common::PopulateRequestContext(request.mutable_context(), config_.node_id, common::GenerateRequestId());
    request.set_leader_id(config_.node_id);
    request.set_prev_lsn(start_lsn - 1);
    request.set_leader_commit(commit_index_);
    for (const auto& entry : entries) {
      *request.add_entries() = entry;
    }

    deltafs::v1::AppendEntriesResponse response;
    auto send_status = SendAppendEntriesLocked(peer, request, &response);
    if (!send_status.ok()) {
      DELTAFS_LOG_WARN("replication transport failed to " + peer.node_id + ": " + send_status.message);
      return send_status;
    }

    if (!response.success()) {
      if (response.last_lsn() < request.prev_lsn()) {
        match = response.last_lsn();
        continue;
      }
      return common::Status::FailedPrecondition("peer rejected append entries")
          .WithDetail("peer", peer.node_id)
          .WithDetail("peer_last_lsn", std::to_string(response.last_lsn()));
    }

    match = response.last_lsn();
  }

  return common::Status::Ok();
}

common::Status ReplicationManager::BroadcastCommitIndexLocked() {
  for (const auto& peer : peers_) {
    if (peer.node_id == config_.node_id) {
      continue;
    }

    deltafs::v1::AppendEntriesRequest request;
    common::PopulateRequestContext(request.mutable_context(), config_.node_id, common::GenerateRequestId());
    request.set_leader_id(config_.node_id);
    request.set_prev_lsn(follower_match_index_[peer.node_id]);
    request.set_leader_commit(commit_index_);

    deltafs::v1::AppendEntriesResponse response;
    auto status = SendAppendEntriesLocked(peer, request, &response);
    if (!status.ok()) {
      DELTAFS_LOG_WARN("commit heartbeat failed to " + peer.node_id + ": " + status.message);
      continue;
    }
    if (response.success()) {
      follower_match_index_[peer.node_id] = response.last_lsn();
    }
  }

  return common::Status::Ok();
}

common::Status ReplicationManager::ReplicateClientEntry(const deltafs::v1::WalEntry& entry_template,
                                                        deltafs::v1::WalEntry* committed_entry) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (config_.node_id != leader_id_) {
    return common::Status::NotLeader("write rejected on follower", leader_id_);
  }

  if (commit_index_ < journal_->last_lsn()) {
    size_t pending_acks = 1;
    for (const auto& peer : peers_) {
      if (peer.node_id == config_.node_id) {
        continue;
      }
      const auto status = ReplicateToPeerLocked(peer, journal_->last_lsn());
      if (status.ok()) {
        ++pending_acks;
      }
    }
    if (!AckSatisfiedLocked(pending_acks)) {
      return common::Status::Unavailable("unable to replicate pending WAL entries to ack threshold", true);
    }

    commit_index_ = journal_->last_lsn();
    auto status = ApplyCommittedUpToLocked(commit_index_);
    if (!status.ok()) {
      return status;
    }
    committed_root_ = callbacks_.current_committed_root();
    status = PersistStateLocked();
    if (!status.ok()) {
      return status;
    }
    BroadcastCommitIndexLocked();
  }

  deltafs::v1::WalEntry appended;
  auto status = journal_->AppendNewEntry(entry_template, &appended);
  if (!status.ok()) {
    return status;
  }

  size_t ack_count = 1;
  for (const auto& peer : peers_) {
    if (peer.node_id == config_.node_id) {
      continue;
    }

    const auto replicate_status = ReplicateToPeerLocked(peer, appended.lsn());
    if (replicate_status.ok()) {
      ++ack_count;
    }
  }

  if (!AckSatisfiedLocked(ack_count)) {
    return common::Status::Unavailable("failed to satisfy replication ack policy", true)
        .WithDetail("acks", std::to_string(ack_count));
  }

  commit_index_ = appended.lsn();

  status = ApplyCommittedUpToLocked(commit_index_);
  if (!status.ok()) {
    return status;
  }

  committed_root_ = callbacks_.current_committed_root();

  status = PersistStateLocked();
  if (!status.ok()) {
    return status;
  }

  BroadcastCommitIndexLocked();

  *committed_entry = appended;
  return common::Status::Ok();
}

common::Status ReplicationManager::ForceConsistencyPoint(bool* forced) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (config_.node_id != leader_id_) {
    return common::Status::NotLeader("force consistency point requires leader", leader_id_);
  }

  if (commit_index_ < journal_->last_lsn()) {
    size_t ack_count = 1;
    for (const auto& peer : peers_) {
      if (peer.node_id == config_.node_id) {
        continue;
      }
      const auto status = ReplicateToPeerLocked(peer, journal_->last_lsn());
      if (status.ok()) {
        ++ack_count;
      }
    }

    if (!AckSatisfiedLocked(ack_count)) {
      return common::Status::Unavailable("unable to force consistency point due to ack policy", true);
    }

    commit_index_ = journal_->last_lsn();
    auto status = ApplyCommittedUpToLocked(commit_index_);
    if (!status.ok()) {
      return status;
    }

    committed_root_ = callbacks_.current_committed_root();
    status = PersistStateLocked();
    if (!status.ok()) {
      return status;
    }
    BroadcastCommitIndexLocked();
    *forced = true;
    return common::Status::Ok();
  }

  *forced = false;
  return common::Status::Ok();
}

common::Status ReplicationManager::HandleAppendEntries(
    const deltafs::v1::AppendEntriesRequest& request,
    deltafs::v1::AppendEntriesResponse* response) {
  std::lock_guard<std::mutex> lock(mutex_);

  response->set_success(false);
  response->set_last_lsn(journal_->last_lsn());
  response->set_commit_index(commit_index_);

  leader_id_ = request.leader_id();

  if (request.prev_lsn() > journal_->last_lsn()) {
    return common::Status::Ok();
  }

  for (const auto& entry : request.entries()) {
    auto status = journal_->AppendReplicatedEntry(entry);
    if (!status.ok()) {
      if (status.code == common::StatusCode::kFailedPrecondition) {
        response->set_last_lsn(journal_->last_lsn());
        return common::Status::Ok();
      }
      return status;
    }
  }

  response->set_last_lsn(journal_->last_lsn());

  if (request.leader_commit() > commit_index_) {
    const uint64_t target = std::min(request.leader_commit(), journal_->last_lsn());
    auto status = ApplyCommittedUpToLocked(target);
    if (!status.ok()) {
      return status;
    }

    commit_index_ = target;
    committed_root_ = callbacks_.current_committed_root();

    status = PersistStateLocked();
    if (!status.ok()) {
      return status;
    }
  }

  response->set_success(true);
  response->set_last_lsn(journal_->last_lsn());
  response->set_commit_index(commit_index_);
  return common::Status::Ok();
}

common::Status ReplicationManager::PromoteToLeader(bool force, bool* promoted) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!force) {
    *promoted = false;
    return common::Status::FailedPrecondition(
        "manual promote disabled without force=true in milestone implementation");
  }

  leader_id_ = config_.node_id;
  *promoted = true;
  return PersistStateLocked();
}

}  // namespace replication
}  // namespace deltafs
