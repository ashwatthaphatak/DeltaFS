#include <chrono>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#include <grpcpp/grpcpp.h>

#include "admin.grpc.pb.h"
#include "deltafs/common/grpc_utils.h"
#include "deltafs/common/status.h"
#include "deltafs/common/utils.h"
#include "metadata.grpc.pb.h"
#include "snapshot.grpc.pb.h"

namespace {

struct CliOptions {
  std::string endpoint = "127.0.0.1:50051";
  std::string node_id = "deltafs_ctl";
  int timeout_ms = 2000;
  int retries = 2;
  std::string request_id;
  std::vector<std::string> args;
};

void PrintUsage(const char* argv0) {
  std::cout << "Usage: " << argv0 << " [--endpoint host:port] [--node-id ID] [--timeout-ms N] [--retries N]"
            << " [--request-id ID] <command>\n"
            << "Commands:\n"
            << "  put <key> <value> [--root-version N]\n"
            << "  get <key>\n"
            << "  snapshot create <name>\n"
            << "  snapshot get <snapshot_id> <key>\n"
            << "  snapshot list\n"
            << "  status\n";
}

bool ParseIntArg(const std::string& value, int* out) {
  try {
    *out = std::stoi(value);
    return true;
  } catch (...) {
    return false;
  }
}

bool ParseOptions(int argc, char** argv, CliOptions* out) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--endpoint" && i + 1 < argc) {
      out->endpoint = argv[++i];
    } else if (arg == "--node-id" && i + 1 < argc) {
      out->node_id = argv[++i];
    } else if (arg == "--timeout-ms" && i + 1 < argc) {
      if (!ParseIntArg(argv[++i], &out->timeout_ms)) {
        return false;
      }
    } else if (arg == "--retries" && i + 1 < argc) {
      if (!ParseIntArg(argv[++i], &out->retries)) {
        return false;
      }
    } else if (arg == "--request-id" && i + 1 < argc) {
      out->request_id = argv[++i];
    } else {
      out->args.push_back(arg);
    }
  }
  return true;
}

template <typename Request, typename Response, typename RpcCaller>
int CallWithRetries(const CliOptions& options,
                    Request* request,
                    Response* response,
                    RpcCaller&& caller,
                    bool retryable_call,
                    const std::string& op_name) {
  const std::string request_id =
      options.request_id.empty() ? deltafs::common::GenerateRequestId() : options.request_id;
  deltafs::common::PopulateRequestContext(request->mutable_context(), options.node_id, request_id);

  for (int attempt = 0; attempt <= options.retries; ++attempt) {
    grpc::ClientContext ctx;
    deltafs::common::AddContextMetadata(request->context(), &ctx);
    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(options.timeout_ms));

    grpc::Status grpc_status = caller(&ctx, *request, response);
    if (grpc_status.ok()) {
      const auto app_status = deltafs::common::StatusFromErrorStatusProto(response->error());
      if (!app_status.ok()) {
        std::cerr << op_name << " failed: " << response->error().code() << " message=\""
                  << response->error().message() << "\"";
        if (response->error().retryable()) {
          std::cerr << " retryable=true";
        }
        std::cerr << " request_id=" << request_id << "\n";
        return 2;
      }
      return 0;
    }

    if (!retryable_call || !deltafs::common::IsRetryableGrpcStatus(grpc_status) ||
        attempt == options.retries) {
      std::cerr << op_name << " transport error: " << grpc_status.error_code() << " "
                << grpc_status.error_message() << " request_id=" << request_id << "\n";
      return 3;
    }
  }

  return 3;
}

int HandlePut(const CliOptions& options,
              deltafs::v1::MetadataService::Stub* metadata_stub,
              const std::vector<std::string>& args) {
  if (args.size() < 3) {
    std::cerr << "put requires <key> <value>\n";
    return 1;
  }

  deltafs::v1::PutKeyRequest request;
  request.set_key(args[1]);
  request.set_value(args[2]);

  for (size_t i = 3; i + 1 < args.size(); ++i) {
    if (args[i] == "--root-version") {
      request.set_root_version_context(std::stoull(args[i + 1]));
      ++i;
    }
  }

  deltafs::v1::PutKeyResponse response;
  const int rc = CallWithRetries(options,
                                 &request,
                                 &response,
                                 [&](grpc::ClientContext* ctx,
                                     const deltafs::v1::PutKeyRequest& req,
                                     deltafs::v1::PutKeyResponse* resp) {
                                   return metadata_stub->PutKey(ctx, req, resp);
                                 },
                                 true,
                                 "put");
  if (rc != 0) {
    return rc;
  }

  std::cout << "ok key=" << request.key() << " block_id=" << response.block_id()
            << " lsn=" << response.lsn() << " root_version=" << response.root_version() << "\n";
  return 0;
}

int HandleGet(const CliOptions& options,
              deltafs::v1::MetadataService::Stub* metadata_stub,
              const std::vector<std::string>& args) {
  if (args.size() < 2) {
    std::cerr << "get requires <key>\n";
    return 1;
  }

  deltafs::v1::GetKeyRequest request;
  request.set_key(args[1]);
  deltafs::v1::GetKeyResponse response;

  const int rc = CallWithRetries(options,
                                 &request,
                                 &response,
                                 [&](grpc::ClientContext* ctx,
                                     const deltafs::v1::GetKeyRequest& req,
                                     deltafs::v1::GetKeyResponse* resp) {
                                   return metadata_stub->GetKey(ctx, req, resp);
                                 },
                                 true,
                                 "get");
  if (rc != 0) {
    return rc;
  }

  if (!response.found()) {
    std::cout << "not_found key=" << request.key() << " root_version=" << response.root_version()
              << "\n";
    return 0;
  }

  std::cout << "found key=" << request.key() << " value=\"" << response.value() << "\" block_id="
            << response.block_id() << " root_version=" << response.root_version() << "\n";
  return 0;
}

int HandleSnapshotCreate(const CliOptions& options,
                         deltafs::v1::SnapshotService::Stub* snapshot_stub,
                         const std::vector<std::string>& args) {
  if (args.size() < 3) {
    std::cerr << "snapshot create requires <name>\n";
    return 1;
  }

  deltafs::v1::CreateSnapshotRequest request;
  request.set_name(args[2]);
  deltafs::v1::CreateSnapshotResponse response;

  const int rc = CallWithRetries(options,
                                 &request,
                                 &response,
                                 [&](grpc::ClientContext* ctx,
                                     const deltafs::v1::CreateSnapshotRequest& req,
                                     deltafs::v1::CreateSnapshotResponse* resp) {
                                   return snapshot_stub->CreateSnapshot(ctx, req, resp);
                                 },
                                 true,
                                 "snapshot create");
  if (rc != 0) {
    return rc;
  }

  std::cout << "snapshot_id=" << response.snapshot_id() << " root_version=" << response.root_version()
            << " lsn=" << response.lsn() << "\n";
  return 0;
}

int HandleSnapshotGet(const CliOptions& options,
                      deltafs::v1::SnapshotService::Stub* snapshot_stub,
                      const std::vector<std::string>& args) {
  if (args.size() < 4) {
    std::cerr << "snapshot get requires <snapshot_id> <key>\n";
    return 1;
  }

  deltafs::v1::ReadAtSnapshotRequest request;
  request.set_snapshot_id(args[2]);
  request.set_key(args[3]);
  deltafs::v1::ReadAtSnapshotResponse response;

  const int rc = CallWithRetries(options,
                                 &request,
                                 &response,
                                 [&](grpc::ClientContext* ctx,
                                     const deltafs::v1::ReadAtSnapshotRequest& req,
                                     deltafs::v1::ReadAtSnapshotResponse* resp) {
                                   return snapshot_stub->ReadAtSnapshot(ctx, req, resp);
                                 },
                                 true,
                                 "snapshot get");
  if (rc != 0) {
    return rc;
  }

  if (!response.found()) {
    std::cout << "not_found snapshot_id=" << request.snapshot_id() << " key=" << request.key() << "\n";
    return 0;
  }

  std::cout << "snapshot_id=" << request.snapshot_id() << " key=" << request.key() << " value=\""
            << response.value() << "\" block_id=" << response.block_id()
            << " root_version=" << response.root_version() << "\n";
  return 0;
}

int HandleSnapshotList(const CliOptions& options,
                       deltafs::v1::SnapshotService::Stub* snapshot_stub,
                       const std::vector<std::string>& args) {
  (void)args;
  deltafs::v1::ListSnapshotsRequest request;
  deltafs::v1::ListSnapshotsResponse response;

  const int rc = CallWithRetries(options,
                                 &request,
                                 &response,
                                 [&](grpc::ClientContext* ctx,
                                     const deltafs::v1::ListSnapshotsRequest& req,
                                     deltafs::v1::ListSnapshotsResponse* resp) {
                                   return snapshot_stub->ListSnapshots(ctx, req, resp);
                                 },
                                 true,
                                 "snapshot list");
  if (rc != 0) {
    return rc;
  }

  for (const auto& snapshot : response.snapshots()) {
    std::cout << snapshot.snapshot_id() << " name=" << snapshot.name()
              << " root_version=" << snapshot.root_version()
              << " created_at_ms=" << snapshot.created_at_unix_ms() << "\n";
  }
  if (response.snapshots().empty()) {
    std::cout << "no_snapshots\n";
  }
  return 0;
}

int HandleStatus(const CliOptions& options,
                 deltafs::v1::AdminService::Stub* admin_stub,
                 const std::vector<std::string>& args) {
  (void)args;
  deltafs::v1::GetStatusRequest request;
  deltafs::v1::GetStatusResponse response;

  const int rc = CallWithRetries(options,
                                 &request,
                                 &response,
                                 [&](grpc::ClientContext* ctx,
                                     const deltafs::v1::GetStatusRequest& req,
                                     deltafs::v1::GetStatusResponse* resp) {
                                   return admin_stub->GetStatus(ctx, req, resp);
                                 },
                                 true,
                                 "status");
  if (rc != 0) {
    return rc;
  }

  std::cout << "node_id=" << response.node_id() << " role=" << response.role()
            << " leader_id=" << response.leader_id() << " commit_index=" << response.commit_index()
            << " last_applied=" << response.last_applied()
            << " committed_root=" << response.committed_root()
            << " snapshots=" << response.snapshots_count() << "\n";

  for (const auto& peer : response.peer_endpoints()) {
    std::cout << "peer=" << peer << "\n";
  }

  for (const auto& path : response.storage_paths()) {
    std::cout << "path[" << path.label() << "]=" << path.path() << "\n";
  }

  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  GOOGLE_PROTOBUF_VERIFY_VERSION;

  CliOptions options;
  if (!ParseOptions(argc, argv, &options)) {
    PrintUsage(argv[0]);
    return 1;
  }

  if (options.args.empty()) {
    PrintUsage(argv[0]);
    return 1;
  }

  auto channel = grpc::CreateChannel(options.endpoint, grpc::InsecureChannelCredentials());
  auto metadata_stub = deltafs::v1::MetadataService::NewStub(channel);
  auto snapshot_stub = deltafs::v1::SnapshotService::NewStub(channel);
  auto admin_stub = deltafs::v1::AdminService::NewStub(channel);

  const std::string& command = options.args[0];
  if (command == "put") {
    return HandlePut(options, metadata_stub.get(), options.args);
  }
  if (command == "get") {
    return HandleGet(options, metadata_stub.get(), options.args);
  }
  if (command == "snapshot") {
    if (options.args.size() < 2) {
      std::cerr << "snapshot requires subcommand create|get|list\n";
      return 1;
    }
    if (options.args[1] == "create") {
      return HandleSnapshotCreate(options, snapshot_stub.get(), options.args);
    }
    if (options.args[1] == "get") {
      return HandleSnapshotGet(options, snapshot_stub.get(), options.args);
    }
    if (options.args[1] == "list") {
      return HandleSnapshotList(options, snapshot_stub.get(), options.args);
    }
    std::cerr << "unknown snapshot subcommand: " << options.args[1] << "\n";
    return 1;
  }
  if (command == "status") {
    return HandleStatus(options, admin_stub.get(), options.args);
  }

  std::cerr << "unknown command: " << command << "\n";
  PrintUsage(argv[0]);
  return 1;
}
