#include "deltafs/services/node_services.h"

#include "deltafs/common/logging.h"
#include "deltafs/services/service_helpers.h"

namespace deltafs {
namespace services {

grpc::Status BlockStoreServiceImpl::PutBlock(grpc::ServerContext*,
                                             const deltafs::v1::PutBlockRequest* request,
                                             deltafs::v1::PutBlockResponse* response) {
  common::ScopedLogContext log_scope(request->context().request_id(), request->context().node_id());
  auto status = ValidateRequestContext(request->context());
  if (!status.ok()) {
    SetResponseError(status, response->mutable_error());
    return grpc::Status::OK;
  }

  std::string block_id;
  status = node_->PutBlockRaw(request->context(), request->data(), &block_id);
  SetResponseError(status, response->mutable_error());
  if (status.ok()) {
    response->set_block_id(block_id);
  }
  return grpc::Status::OK;
}

grpc::Status BlockStoreServiceImpl::GetBlock(grpc::ServerContext*,
                                             const deltafs::v1::GetBlockRequest* request,
                                             deltafs::v1::GetBlockResponse* response) {
  common::ScopedLogContext log_scope(request->context().request_id(), request->context().node_id());
  auto status = ValidateRequestContext(request->context());
  if (!status.ok()) {
    SetResponseError(status, response->mutable_error());
    return grpc::Status::OK;
  }

  std::string data;
  bool found = false;
  status = node_->GetBlockRaw(request->block_id(), &data, &found);
  SetResponseError(status, response->mutable_error());
  response->set_found(found);
  if (status.ok() && found) {
    response->set_data(data);
  }

  return grpc::Status::OK;
}

grpc::Status JournalServiceImpl::AppendEntry(grpc::ServerContext*,
                                             const deltafs::v1::AppendEntryRequest* request,
                                             deltafs::v1::AppendEntryResponse* response) {
  common::ScopedLogContext log_scope(request->context().request_id(), request->context().node_id());
  auto status = ValidateRequestContext(request->context());
  if (!status.ok()) {
    SetResponseError(status, response->mutable_error());
    return grpc::Status::OK;
  }

  uint64_t lsn = 0;
  status = node_->JournalAppend(request->context(), request->entry(), &lsn);
  SetResponseError(status, response->mutable_error());
  if (status.ok()) {
    response->set_lsn(lsn);
  }

  return grpc::Status::OK;
}

grpc::Status JournalServiceImpl::GetJournalStatus(grpc::ServerContext*,
                                                  const deltafs::v1::GetJournalStatusRequest* request,
                                                  deltafs::v1::GetJournalStatusResponse* response) {
  common::ScopedLogContext log_scope(request->context().request_id(), request->context().node_id());
  auto status = ValidateRequestContext(request->context());
  if (!status.ok()) {
    SetResponseError(status, response->mutable_error());
    return grpc::Status::OK;
  }

  replication::AdminStatus admin_status;
  status = node_->GetAdminStatus(&admin_status);
  if (!status.ok()) {
    SetResponseError(status, response->mutable_error());
    return grpc::Status::OK;
  }

  response->set_last_lsn(node_->JournalLastLsn());
  response->set_commit_index(admin_status.commit_index);
  response->set_last_applied(admin_status.last_applied);
  response->set_committed_root(admin_status.committed_root);

  deltafs::v1::WalEntry last_entry;
  auto last_status = node_->JournalLastEntry(&last_entry);
  if (last_status.ok()) {
    *response->mutable_last_entry() = last_entry;
  }

  SetResponseError(common::Status::Ok(), response->mutable_error());
  return grpc::Status::OK;
}

grpc::Status MetadataServiceImpl::PutKey(grpc::ServerContext*,
                                         const deltafs::v1::PutKeyRequest* request,
                                         deltafs::v1::PutKeyResponse* response) {
  common::ScopedLogContext log_scope(request->context().request_id(), request->context().node_id());
  auto status = ValidateRequestContext(request->context());
  if (!status.ok()) {
    SetResponseError(status, response->mutable_error());
    return grpc::Status::OK;
  }

  replication::PutKeyResult result;
  status = node_->PutKey(request->context(),
                         request->key(),
                         request->value(),
                         request->root_version_context(),
                         &result);

  SetResponseError(status, response->mutable_error());
  if (status.ok()) {
    response->set_applied(result.applied);
    response->set_lsn(result.lsn);
    response->set_root_version(result.root_version);
    response->set_block_id(result.block_id);
  }

  return grpc::Status::OK;
}

grpc::Status MetadataServiceImpl::GetKey(grpc::ServerContext*,
                                         const deltafs::v1::GetKeyRequest* request,
                                         deltafs::v1::GetKeyResponse* response) {
  common::ScopedLogContext log_scope(request->context().request_id(), request->context().node_id());
  auto status = ValidateRequestContext(request->context());
  if (!status.ok()) {
    SetResponseError(status, response->mutable_error());
    return grpc::Status::OK;
  }

  replication::GetKeyResult result;
  status = node_->GetKey(request->key(), request->snapshot_id(), request->root_version(), &result);

  SetResponseError(status, response->mutable_error());
  if (status.ok()) {
    response->set_found(result.found);
    response->set_root_version(result.root_version);
    if (result.found) {
      response->set_block_id(result.block_id);
      response->set_value(result.value);
    }
  }

  return grpc::Status::OK;
}

grpc::Status SnapshotServiceImpl::CreateSnapshot(grpc::ServerContext*,
                                                 const deltafs::v1::CreateSnapshotRequest* request,
                                                 deltafs::v1::CreateSnapshotResponse* response) {
  common::ScopedLogContext log_scope(request->context().request_id(), request->context().node_id());
  auto status = ValidateRequestContext(request->context());
  if (!status.ok()) {
    SetResponseError(status, response->mutable_error());
    return grpc::Status::OK;
  }

  replication::CreateSnapshotResult result;
  status = node_->CreateSnapshot(request->context(), request->name(), &result);
  SetResponseError(status, response->mutable_error());
  if (status.ok()) {
    response->set_snapshot_id(result.snapshot_id);
    response->set_root_version(result.root_version);
    response->set_lsn(result.lsn);
  }

  return grpc::Status::OK;
}

grpc::Status SnapshotServiceImpl::ListSnapshots(grpc::ServerContext*,
                                                const deltafs::v1::ListSnapshotsRequest* request,
                                                deltafs::v1::ListSnapshotsResponse* response) {
  common::ScopedLogContext log_scope(request->context().request_id(), request->context().node_id());
  auto status = ValidateRequestContext(request->context());
  if (!status.ok()) {
    SetResponseError(status, response->mutable_error());
    return grpc::Status::OK;
  }

  const auto snapshots = node_->ListSnapshots();
  for (const auto& snapshot : snapshots) {
    auto* out = response->add_snapshots();
    out->set_snapshot_id(snapshot.snapshot_id);
    out->set_name(snapshot.name);
    out->set_root_version(snapshot.root_version);
    out->set_created_at_unix_ms(snapshot.created_at_unix_ms);
  }

  SetResponseError(common::Status::Ok(), response->mutable_error());
  return grpc::Status::OK;
}

grpc::Status SnapshotServiceImpl::ReadAtSnapshot(grpc::ServerContext*,
                                                 const deltafs::v1::ReadAtSnapshotRequest* request,
                                                 deltafs::v1::ReadAtSnapshotResponse* response) {
  common::ScopedLogContext log_scope(request->context().request_id(), request->context().node_id());
  auto status = ValidateRequestContext(request->context());
  if (!status.ok()) {
    SetResponseError(status, response->mutable_error());
    return grpc::Status::OK;
  }

  replication::GetKeyResult result;
  status = node_->ReadAtSnapshot(request->snapshot_id(), request->key(), &result);

  SetResponseError(status, response->mutable_error());
  if (status.ok()) {
    response->set_found(result.found);
    response->set_root_version(result.root_version);
    if (result.found) {
      response->set_block_id(result.block_id);
      response->set_value(result.value);
    }
  }

  return grpc::Status::OK;
}

grpc::Status ReplicationServiceImpl::AppendEntries(
    grpc::ServerContext*,
    const deltafs::v1::AppendEntriesRequest* request,
    deltafs::v1::AppendEntriesResponse* response) {
  common::ScopedLogContext log_scope(request->context().request_id(), request->context().node_id());

  auto status = node_->HandleAppendEntries(*request, response);
  SetResponseError(status, response->mutable_error());

  return grpc::Status::OK;
}

grpc::Status AdminServiceImpl::GetStatus(grpc::ServerContext*,
                                         const deltafs::v1::GetStatusRequest* request,
                                         deltafs::v1::GetStatusResponse* response) {
  common::ScopedLogContext log_scope(request->context().request_id(), request->context().node_id());
  auto status = ValidateRequestContext(request->context());
  if (!status.ok()) {
    SetResponseError(status, response->mutable_error());
    return grpc::Status::OK;
  }

  replication::AdminStatus status_out;
  status = node_->GetAdminStatus(&status_out);
  SetResponseError(status, response->mutable_error());
  if (!status.ok()) {
    return grpc::Status::OK;
  }

  response->set_node_id(status_out.node_id);
  response->set_role(status_out.role);
  response->set_leader_id(status_out.leader_id);
  response->set_commit_index(status_out.commit_index);
  response->set_last_applied(status_out.last_applied);
  response->set_committed_root(status_out.committed_root);
  response->set_snapshots_count(status_out.snapshots_count);
  for (const auto& peer : status_out.peers) {
    response->add_peer_endpoints(peer.endpoint);
  }

  auto* p1 = response->add_storage_paths();
  p1->set_label("blocks");
  p1->set_path(node_->config().data_dir + "/blocks");
  auto* p2 = response->add_storage_paths();
  p2->set_label("wal");
  p2->set_path(node_->config().data_dir + "/wal");
  auto* p3 = response->add_storage_paths();
  p3->set_label("metadata");
  p3->set_path(node_->config().data_dir + "/metadata");
  auto* p4 = response->add_storage_paths();
  p4->set_label("snapshots");
  p4->set_path(node_->config().data_dir + "/snapshots");
  auto* p5 = response->add_storage_paths();
  p5->set_label("dedupe");
  p5->set_path(node_->config().data_dir + "/dedupe");

  return grpc::Status::OK;
}

grpc::Status AdminServiceImpl::ForceConsistencyPoint(
    grpc::ServerContext*,
    const deltafs::v1::ForceConsistencyPointRequest* request,
    deltafs::v1::ForceConsistencyPointResponse* response) {
  common::ScopedLogContext log_scope(request->context().request_id(), request->context().node_id());
  auto status = ValidateRequestContext(request->context());
  if (!status.ok()) {
    SetResponseError(status, response->mutable_error());
    return grpc::Status::OK;
  }

  bool forced = false;
  uint64_t commit_index = 0;
  uint64_t committed_root = 0;
  status = node_->ForceConsistencyPoint(&forced, &commit_index, &committed_root);

  SetResponseError(status, response->mutable_error());
  if (status.ok()) {
    response->set_forced(forced);
    response->set_commit_index(commit_index);
    response->set_committed_root(committed_root);
  }

  return grpc::Status::OK;
}

grpc::Status AdminServiceImpl::PromoteToLeader(
    grpc::ServerContext*,
    const deltafs::v1::PromoteToLeaderRequest* request,
    deltafs::v1::PromoteToLeaderResponse* response) {
  common::ScopedLogContext log_scope(request->context().request_id(), request->context().node_id());
  auto status = ValidateRequestContext(request->context());
  if (!status.ok()) {
    SetResponseError(status, response->mutable_error());
    return grpc::Status::OK;
  }

  bool promoted = false;
  std::string role;
  status = node_->PromoteToLeader(request->force(), &promoted, &role);

  SetResponseError(status, response->mutable_error());
  response->set_promoted(promoted);
  response->set_role(role);
  return grpc::Status::OK;
}

}  // namespace services
}  // namespace deltafs
