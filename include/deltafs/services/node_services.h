#pragma once

#include <grpcpp/grpcpp.h>

#include "admin.grpc.pb.h"
#include "blockstore.grpc.pb.h"
#include "journal.grpc.pb.h"
#include "metadata.grpc.pb.h"
#include "replication.grpc.pb.h"
#include "snapshot.grpc.pb.h"

#include "deltafs/replication/replica_node.h"

namespace deltafs {
namespace services {

class BlockStoreServiceImpl final : public deltafs::v1::BlockStoreService::Service {
 public:
  explicit BlockStoreServiceImpl(replication::ReplicaNode* node) : node_(node) {}

  grpc::Status PutBlock(grpc::ServerContext* context,
                        const deltafs::v1::PutBlockRequest* request,
                        deltafs::v1::PutBlockResponse* response) override;

  grpc::Status GetBlock(grpc::ServerContext* context,
                        const deltafs::v1::GetBlockRequest* request,
                        deltafs::v1::GetBlockResponse* response) override;

 private:
  replication::ReplicaNode* node_;
};

class JournalServiceImpl final : public deltafs::v1::JournalService::Service {
 public:
  explicit JournalServiceImpl(replication::ReplicaNode* node) : node_(node) {}

  grpc::Status AppendEntry(grpc::ServerContext* context,
                           const deltafs::v1::AppendEntryRequest* request,
                           deltafs::v1::AppendEntryResponse* response) override;

  grpc::Status GetJournalStatus(grpc::ServerContext* context,
                                const deltafs::v1::GetJournalStatusRequest* request,
                                deltafs::v1::GetJournalStatusResponse* response) override;

 private:
  replication::ReplicaNode* node_;
};

class MetadataServiceImpl final : public deltafs::v1::MetadataService::Service {
 public:
  explicit MetadataServiceImpl(replication::ReplicaNode* node) : node_(node) {}

  grpc::Status PutKey(grpc::ServerContext* context,
                      const deltafs::v1::PutKeyRequest* request,
                      deltafs::v1::PutKeyResponse* response) override;

  grpc::Status GetKey(grpc::ServerContext* context,
                      const deltafs::v1::GetKeyRequest* request,
                      deltafs::v1::GetKeyResponse* response) override;

 private:
  replication::ReplicaNode* node_;
};

class SnapshotServiceImpl final : public deltafs::v1::SnapshotService::Service {
 public:
  explicit SnapshotServiceImpl(replication::ReplicaNode* node) : node_(node) {}

  grpc::Status CreateSnapshot(grpc::ServerContext* context,
                              const deltafs::v1::CreateSnapshotRequest* request,
                              deltafs::v1::CreateSnapshotResponse* response) override;

  grpc::Status ListSnapshots(grpc::ServerContext* context,
                             const deltafs::v1::ListSnapshotsRequest* request,
                             deltafs::v1::ListSnapshotsResponse* response) override;

  grpc::Status ReadAtSnapshot(grpc::ServerContext* context,
                              const deltafs::v1::ReadAtSnapshotRequest* request,
                              deltafs::v1::ReadAtSnapshotResponse* response) override;

 private:
  replication::ReplicaNode* node_;
};

class ReplicationServiceImpl final : public deltafs::v1::ReplicationService::Service {
 public:
  explicit ReplicationServiceImpl(replication::ReplicaNode* node) : node_(node) {}

  grpc::Status AppendEntries(grpc::ServerContext* context,
                             const deltafs::v1::AppendEntriesRequest* request,
                             deltafs::v1::AppendEntriesResponse* response) override;

 private:
  replication::ReplicaNode* node_;
};

class AdminServiceImpl final : public deltafs::v1::AdminService::Service {
 public:
  explicit AdminServiceImpl(replication::ReplicaNode* node) : node_(node) {}

  grpc::Status GetStatus(grpc::ServerContext* context,
                         const deltafs::v1::GetStatusRequest* request,
                         deltafs::v1::GetStatusResponse* response) override;

  grpc::Status ForceConsistencyPoint(
      grpc::ServerContext* context,
      const deltafs::v1::ForceConsistencyPointRequest* request,
      deltafs::v1::ForceConsistencyPointResponse* response) override;

  grpc::Status PromoteToLeader(grpc::ServerContext* context,
                               const deltafs::v1::PromoteToLeaderRequest* request,
                               deltafs::v1::PromoteToLeaderResponse* response) override;

 private:
  replication::ReplicaNode* node_;
};

}  // namespace services
}  // namespace deltafs
