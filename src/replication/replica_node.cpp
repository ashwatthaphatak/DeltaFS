#include "deltafs/replication/replica_node.h"

#include "deltafs/common/logging.h"
#include "deltafs/common/utils.h"

namespace deltafs {
namespace replication {

ReplicaNode::ReplicaNode(common::NodeConfig config)
    : config_(std::move(config)),
      block_store_(config_.data_dir + "/blocks"),
      journal_(config_.data_dir + "/wal"),
      metadata_(config_.data_dir + "/metadata"),
      snapshot_manager_(config_.data_dir + "/snapshots"),
      dedupe_store_(config_.data_dir + "/dedupe") {}

common::Status ReplicaNode::Initialize() {
  std::string error;
  if (!common::EnsureDirectory(config_.data_dir, &error)) {
    return common::Status::Internal("failed to create node data dir: " + error);
  }

  auto status = block_store_.Initialize();
  if (!status.ok()) {
    return status;
  }

  status = journal_.Initialize();
  if (!status.ok()) {
    return status;
  }

  status = metadata_.Initialize();
  if (!status.ok()) {
    return status;
  }

  status = snapshot_manager_.Initialize();
  if (!status.ok()) {
    return status;
  }

  status = dedupe_store_.Load();
  if (!status.ok()) {
    return status;
  }

  common::Status membership_status;
  membership_provider_ = membership::CreateMembershipProvider(config_, &membership_status);
  if (!membership_status.ok()) {
    return membership_status;
  }

  ReplicationManager::Callbacks callbacks;
  callbacks.apply_entry = [this](const deltafs::v1::WalEntry& entry) { return ApplyWalEntry(entry); };
  callbacks.current_committed_root = [this]() { return metadata_.committed_root(); };

  replication_manager_ =
      std::make_unique<ReplicationManager>(config_, membership_provider_.get(), &journal_, callbacks);
  status = replication_manager_->Initialize();
  if (!status.ok()) {
    return status;
  }

  return common::Status::Ok();
}

common::Status ReplicaNode::RecordDedupe(const common::DedupeRecord& record) {
  auto status = dedupe_store_.Remember(record);
  if (!status.ok()) {
    DELTAFS_LOG_WARN("failed to persist dedupe record request_id=" + record.request_id +
                     " err=" + status.message);
  }
  return common::Status::Ok();
}

common::Status ReplicaNode::ApplyWalEntry(const deltafs::v1::WalEntry& entry) {
  switch (entry.operation()) {
    case deltafs::v1::WAL_OPERATION_PUT_KEY: {
      auto status = block_store_.PutBlockWithId(entry.block_id(), entry.value());
      if (!status.ok()) {
        return status;
      }

      status = metadata_.ApplyReplicatedPut(entry.key(),
                                            entry.block_id(),
                                            entry.base_root_version(),
                                            entry.new_root_version());
      if (!status.ok()) {
        return status;
      }

      status = metadata_.CommitRoot(entry.new_root_version());
      if (!status.ok()) {
        return status;
      }

      if (!entry.request_id().empty()) {
        common::DedupeRecord record;
        record.request_id = entry.request_id();
        record.operation = "put_key";
        record.success = true;
        record.lsn = entry.lsn();
        record.root_version = entry.new_root_version();
        record.block_id = entry.block_id();
        record.timestamp_unix_ms = entry.timestamp_unix_ms();
        RecordDedupe(record);
      }
      return common::Status::Ok();
    }

    case deltafs::v1::WAL_OPERATION_CREATE_SNAPSHOT: {
      auto status = snapshot_manager_.ApplyReplicatedSnapshot(entry.snapshot_id(),
                                                              entry.snapshot_name(),
                                                              entry.base_root_version(),
                                                              entry.timestamp_unix_ms());
      if (!status.ok()) {
        return status;
      }

      status = metadata_.CommitRoot(entry.base_root_version());
      if (!status.ok()) {
        return status;
      }

      if (!entry.request_id().empty()) {
        common::DedupeRecord record;
        record.request_id = entry.request_id();
        record.operation = "create_snapshot";
        record.success = true;
        record.lsn = entry.lsn();
        record.root_version = entry.base_root_version();
        record.snapshot_id = entry.snapshot_id();
        record.timestamp_unix_ms = entry.timestamp_unix_ms();
        RecordDedupe(record);
      }
      return common::Status::Ok();
    }

    case deltafs::v1::WAL_OPERATION_FORCE_CONSISTENCY_POINT: {
      auto status = metadata_.CommitRoot(entry.base_root_version());
      if (!status.ok()) {
        return status;
      }

      if (!entry.request_id().empty()) {
        common::DedupeRecord record;
        record.request_id = entry.request_id();
        record.operation = "force_consistency_point";
        record.success = true;
        record.lsn = entry.lsn();
        record.root_version = entry.base_root_version();
        record.timestamp_unix_ms = entry.timestamp_unix_ms();
        RecordDedupe(record);
      }

      return common::Status::Ok();
    }

    case deltafs::v1::WAL_OPERATION_UNSPECIFIED:
    default:
      return common::Status::InvalidArgument("unsupported WAL operation");
  }
}

common::Status ReplicaNode::PutKey(const deltafs::v1::RequestContext& context,
                                   const std::string& key,
                                   const std::string& value,
                                   uint64_t root_version_context,
                                   PutKeyResult* result) {
  if (key.empty()) {
    return common::Status::InvalidArgument("key cannot be empty");
  }

  std::lock_guard<std::mutex> lock(write_mutex_);

  common::DedupeRecord dedupe_record;
  if (dedupe_store_.Lookup(context.request_id(), &dedupe_record)) {
    result->applied = dedupe_record.success;
    result->lsn = dedupe_record.lsn;
    result->root_version = dedupe_record.root_version;
    result->block_id = dedupe_record.block_id;
    return common::Status::Ok();
  }

  const uint64_t current_root = metadata_.committed_root();
  const uint64_t base_root = root_version_context == 0 ? current_root : root_version_context;
  if (base_root != current_root) {
    return common::Status::FailedPrecondition("root_version_context does not match committed root")
        .WithDetail("committed_root", std::to_string(current_root));
  }

  std::string block_id;
  auto status = block_store_.PutBlock(value, &block_id);
  if (!status.ok()) {
    return status;
  }

  deltafs::v1::WalEntry entry;
  entry.set_request_id(context.request_id());
  entry.set_origin_node_id(context.node_id());
  entry.set_operation(deltafs::v1::WAL_OPERATION_PUT_KEY);
  entry.set_key(key);
  entry.set_block_id(block_id);
  entry.set_value(value);
  entry.set_base_root_version(base_root);
  entry.set_new_root_version(base_root + 1);
  entry.set_timestamp_unix_ms(common::NowUnixMillis());

  deltafs::v1::WalEntry committed;
  status = replication_manager_->ReplicateClientEntry(entry, &committed);
  if (!status.ok()) {
    return status;
  }

  result->applied = true;
  result->lsn = committed.lsn();
  result->root_version = committed.new_root_version();
  result->block_id = committed.block_id();

  common::DedupeRecord record;
  record.request_id = context.request_id();
  record.operation = "put_key";
  record.success = true;
  record.lsn = result->lsn;
  record.root_version = result->root_version;
  record.block_id = result->block_id;
  record.timestamp_unix_ms = common::NowUnixMillis();
  RecordDedupe(record);

  return common::Status::Ok();
}

common::Status ReplicaNode::GetKey(const std::string& key,
                                   const std::string& snapshot_id,
                                   uint64_t root_version,
                                   GetKeyResult* result) {
  uint64_t effective_root = root_version;
  if (!snapshot_id.empty()) {
    auto status = snapshot_manager_.GetSnapshotRoot(snapshot_id, &effective_root);
    if (!status.ok()) {
      return status;
    }
  }

  bool found = false;
  std::string block_id;
  auto status = metadata_.GetBlockForKey(key, effective_root, &block_id, &found);
  if (!status.ok()) {
    return status;
  }

  if (!found) {
    result->found = false;
    result->block_id.clear();
    result->value.clear();
    result->root_version = (effective_root == 0) ? metadata_.committed_root() : effective_root;
    return common::Status::Ok();
  }

  std::string value;
  bool block_found = false;
  status = block_store_.GetBlock(block_id, &value, &block_found);
  if (!status.ok()) {
    return status;
  }

  if (!block_found) {
    return common::Status::NotFound("block missing for key: " + key)
        .WithDetail("block_id", block_id)
        .WithDetail("root_version", std::to_string(effective_root));
  }

  result->found = true;
  result->block_id = block_id;
  result->value = std::move(value);
  result->root_version = (effective_root == 0) ? metadata_.committed_root() : effective_root;
  return common::Status::Ok();
}

common::Status ReplicaNode::CreateSnapshot(const deltafs::v1::RequestContext& context,
                                           const std::string& name,
                                           CreateSnapshotResult* result) {
  if (name.empty()) {
    return common::Status::InvalidArgument("snapshot name cannot be empty");
  }

  std::lock_guard<std::mutex> lock(write_mutex_);

  common::DedupeRecord dedupe_record;
  if (dedupe_store_.Lookup(context.request_id(), &dedupe_record)) {
    result->snapshot_id = dedupe_record.snapshot_id;
    result->root_version = dedupe_record.root_version;
    result->lsn = dedupe_record.lsn;
    return common::Status::Ok();
  }

  const uint64_t root = metadata_.committed_root();

  deltafs::v1::WalEntry entry;
  entry.set_request_id(context.request_id());
  entry.set_origin_node_id(context.node_id());
  entry.set_operation(deltafs::v1::WAL_OPERATION_CREATE_SNAPSHOT);
  entry.set_snapshot_id("snap-" + common::GenerateRequestId());
  entry.set_snapshot_name(name);
  entry.set_base_root_version(root);
  entry.set_new_root_version(root);
  entry.set_timestamp_unix_ms(common::NowUnixMillis());

  deltafs::v1::WalEntry committed;
  auto status = replication_manager_->ReplicateClientEntry(entry, &committed);
  if (!status.ok()) {
    return status;
  }

  result->snapshot_id = committed.snapshot_id();
  result->root_version = committed.base_root_version();
  result->lsn = committed.lsn();

  common::DedupeRecord record;
  record.request_id = context.request_id();
  record.operation = "create_snapshot";
  record.success = true;
  record.lsn = result->lsn;
  record.root_version = result->root_version;
  record.snapshot_id = result->snapshot_id;
  record.timestamp_unix_ms = common::NowUnixMillis();
  RecordDedupe(record);

  return common::Status::Ok();
}

common::Status ReplicaNode::ReadAtSnapshot(const std::string& snapshot_id,
                                           const std::string& key,
                                           GetKeyResult* result) {
  return GetKey(key, snapshot_id, 0, result);
}

std::vector<snapshot::SnapshotRecord> ReplicaNode::ListSnapshots() const {
  return snapshot_manager_.ListSnapshots();
}

common::Status ReplicaNode::PutBlockRaw(const deltafs::v1::RequestContext& context,
                                        const std::string& data,
                                        std::string* block_id) {
  common::DedupeRecord record;
  if (dedupe_store_.Lookup(context.request_id(), &record) && !record.block_id.empty()) {
    *block_id = record.block_id;
    return common::Status::Ok();
  }

  auto status = block_store_.PutBlock(data, block_id);
  if (!status.ok()) {
    return status;
  }

  common::DedupeRecord new_record;
  new_record.request_id = context.request_id();
  new_record.operation = "put_block";
  new_record.success = true;
  new_record.block_id = *block_id;
  new_record.timestamp_unix_ms = common::NowUnixMillis();
  RecordDedupe(new_record);

  return common::Status::Ok();
}

common::Status ReplicaNode::GetBlockRaw(const std::string& block_id,
                                        std::string* data,
                                        bool* found) const {
  return block_store_.GetBlock(block_id, data, found);
}

common::Status ReplicaNode::JournalAppend(const deltafs::v1::RequestContext& context,
                                          const deltafs::v1::WalEntry& entry,
                                          uint64_t* lsn) {
  std::lock_guard<std::mutex> lock(write_mutex_);

  common::DedupeRecord dedupe_record;
  if (dedupe_store_.Lookup(context.request_id(), &dedupe_record)) {
    *lsn = dedupe_record.lsn;
    return common::Status::Ok();
  }

  deltafs::v1::WalEntry normalized = entry;
  if (normalized.request_id().empty()) {
    normalized.set_request_id(context.request_id());
  }
  if (normalized.origin_node_id().empty()) {
    normalized.set_origin_node_id(context.node_id());
  }
  if (normalized.timestamp_unix_ms() == 0) {
    normalized.set_timestamp_unix_ms(common::NowUnixMillis());
  }

  if (normalized.operation() == deltafs::v1::WAL_OPERATION_PUT_KEY) {
    if (normalized.key().empty()) {
      return common::Status::InvalidArgument("journal append put_key requires key");
    }

    const uint64_t base_root = metadata_.committed_root();
    if (normalized.base_root_version() == 0) {
      normalized.set_base_root_version(base_root);
    }
    if (normalized.new_root_version() == 0) {
      normalized.set_new_root_version(normalized.base_root_version() + 1);
    }

    if (normalized.block_id().empty()) {
      std::string block_id;
      auto status = block_store_.PutBlock(normalized.value(), &block_id);
      if (!status.ok()) {
        return status;
      }
      normalized.set_block_id(block_id);
    }
  } else if (normalized.operation() == deltafs::v1::WAL_OPERATION_CREATE_SNAPSHOT) {
    if (normalized.snapshot_id().empty()) {
      normalized.set_snapshot_id("snap-" + common::GenerateRequestId());
    }
    if (normalized.base_root_version() == 0) {
      normalized.set_base_root_version(metadata_.committed_root());
    }
    normalized.set_new_root_version(normalized.base_root_version());
  } else if (normalized.operation() == deltafs::v1::WAL_OPERATION_FORCE_CONSISTENCY_POINT) {
    normalized.set_base_root_version(metadata_.committed_root());
    normalized.set_new_root_version(normalized.base_root_version());
  } else {
    return common::Status::InvalidArgument("unsupported operation for journal append");
  }

  deltafs::v1::WalEntry committed;
  auto status = replication_manager_->ReplicateClientEntry(normalized, &committed);
  if (!status.ok()) {
    return status;
  }

  *lsn = committed.lsn();

  common::DedupeRecord record;
  record.request_id = context.request_id();
  record.operation = "journal_append";
  record.success = true;
  record.lsn = committed.lsn();
  record.root_version = committed.new_root_version();
  record.block_id = committed.block_id();
  record.snapshot_id = committed.snapshot_id();
  record.timestamp_unix_ms = common::NowUnixMillis();
  RecordDedupe(record);

  return common::Status::Ok();
}

common::Status ReplicaNode::HandleAppendEntries(const deltafs::v1::AppendEntriesRequest& request,
                                                deltafs::v1::AppendEntriesResponse* response) {
  return replication_manager_->HandleAppendEntries(request, response);
}

common::Status ReplicaNode::GetAdminStatus(AdminStatus* status) const {
  status->node_id = config_.node_id;
  status->role = replication_manager_->role();
  status->leader_id = replication_manager_->leader_id();
  status->commit_index = replication_manager_->commit_index();
  status->last_applied = replication_manager_->last_applied();
  status->committed_root = metadata_.committed_root();
  status->snapshots_count = snapshot_manager_.ListSnapshots().size();
  status->peers = replication_manager_->peers();
  return common::Status::Ok();
}

common::Status ReplicaNode::ForceConsistencyPoint(bool* forced,
                                                  uint64_t* commit_index,
                                                  uint64_t* committed_root) {
  auto status = replication_manager_->ForceConsistencyPoint(forced);
  if (!status.ok()) {
    return status;
  }
  *commit_index = replication_manager_->commit_index();
  *committed_root = metadata_.committed_root();
  return common::Status::Ok();
}

common::Status ReplicaNode::PromoteToLeader(bool force, bool* promoted, std::string* role) {
  auto status = replication_manager_->PromoteToLeader(force, promoted);
  if (!status.ok()) {
    return status;
  }
  *role = replication_manager_->role();
  return common::Status::Ok();
}

uint64_t ReplicaNode::JournalLastLsn() const { return journal_.last_lsn(); }

common::Status ReplicaNode::JournalLastEntry(deltafs::v1::WalEntry* entry) const {
  const uint64_t lsn = journal_.last_lsn();
  if (lsn == 0) {
    return common::Status::NotFound("journal empty");
  }
  return journal_.GetEntry(lsn, entry);
}

}  // namespace replication
}  // namespace deltafs
