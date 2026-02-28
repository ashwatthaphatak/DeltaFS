#include <csignal>
#include <iostream>
#include <memory>
#include <string>

#include <grpcpp/grpcpp.h>

#include "deltafs/common/config.h"
#include "deltafs/common/logging.h"
#include "deltafs/replication/replica_node.h"
#include "deltafs/services/node_services.h"
#include "deltafs/services/request_context_interceptor.h"

namespace {

std::shared_ptr<grpc::Server> g_server;

void HandleSignal(int) {
  if (g_server != nullptr) {
    g_server->Shutdown();
  }
}

std::string ParseConfigPath(int argc, char** argv) {
  std::string config_path;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if ((arg == "--config" || arg == "-c") && i + 1 < argc) {
      config_path = argv[++i];
    }
  }
  return config_path;
}

}  // namespace

int main(int argc, char** argv) {
  GOOGLE_PROTOBUF_VERIFY_VERSION;

  const std::string config_path = ParseConfigPath(argc, argv);
  if (config_path.empty()) {
    std::cerr << "Usage: " << argv[0] << " --config /path/to/node.yaml\n";
    return 1;
  }

  deltafs::common::NodeConfig config;
  auto status = deltafs::common::LoadNodeConfig(config_path, &config);
  if (!status.ok()) {
    std::cerr << "Failed to load config: " << status.message << "\n";
    return 1;
  }

  deltafs::common::SetLogContext("startup", config.node_id);
  DELTAFS_LOG_INFO("starting deltafs_node listen=" + config.listen_address +
                   " data_dir=" + config.data_dir +
                   " membership=" + config.membership_provider +
                   " ack_policy=" + deltafs::common::AckPolicyToString(config.ack_policy));
  deltafs::common::ClearLogContext();

  deltafs::replication::ReplicaNode node(config);
  status = node.Initialize();
  if (!status.ok()) {
    std::cerr << "Node initialization failed: " << status.message << "\n";
    return 1;
  }

  deltafs::services::BlockStoreServiceImpl blockstore_service(&node);
  deltafs::services::JournalServiceImpl journal_service(&node);
  deltafs::services::MetadataServiceImpl metadata_service(&node);
  deltafs::services::SnapshotServiceImpl snapshot_service(&node);
  deltafs::services::ReplicationServiceImpl replication_service(&node);
  deltafs::services::AdminServiceImpl admin_service(&node);

  grpc::ServerBuilder builder;
  builder.AddListeningPort(config.listen_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&blockstore_service);
  builder.RegisterService(&journal_service);
  builder.RegisterService(&metadata_service);
  builder.RegisterService(&snapshot_service);
  builder.RegisterService(&replication_service);
  builder.RegisterService(&admin_service);
  builder.SetMaxReceiveMessageSize(32 * 1024 * 1024);
  builder.SetMaxSendMessageSize(32 * 1024 * 1024);

  builder.experimental().SetInterceptorCreators(deltafs::services::CreateServerInterceptorFactories());

  g_server = builder.BuildAndStart();
  if (!g_server) {
    std::cerr << "Failed to start gRPC server\n";
    return 1;
  }

  std::signal(SIGINT, HandleSignal);
  std::signal(SIGTERM, HandleSignal);

  deltafs::common::SetLogContext("startup", config.node_id);
  DELTAFS_LOG_INFO("deltafs_node is serving on " + config.listen_address);
  deltafs::common::ClearLogContext();

  g_server->Wait();

  deltafs::common::SetLogContext("shutdown", config.node_id);
  DELTAFS_LOG_INFO("deltafs_node stopped");
  deltafs::common::ClearLogContext();

  google::protobuf::ShutdownProtobufLibrary();
  return 0;
}
