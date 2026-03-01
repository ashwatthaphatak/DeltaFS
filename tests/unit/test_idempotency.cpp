#include <filesystem>
#include <iostream>

#include "deltafs/common/config.h"
#include "deltafs/common/utils.h"
#include "deltafs/replication/replica_node.h"

int main() {
  const std::string base = "/tmp/deltafs_test_idempotency_" + deltafs::common::GenerateRequestId();
  std::filesystem::create_directories(base);

  deltafs::common::NodeConfig config;
  config.node_id = "node0";
  config.listen_address = "127.0.0.1:0";
  config.advertised_address = "node0:50051";
  config.leader_id = "node0";
  config.membership_provider = "compose";
  config.data_dir = base + "/data";
  config.ack_policy = deltafs::common::AckPolicy::kOne;
  config.peers = {{"node0", "node0:50051"}};

  deltafs::replication::ReplicaNode node(config);
  auto status = node.Initialize();
  if (!status.ok()) {
    std::cerr << "node init failed: " << status.message << "\n";
    return 1;
  }

  deltafs::v1::RequestContext request_context;
  request_context.set_request_id("fixed-request-id");
  request_context.set_node_id("client0");

  deltafs::replication::PutKeyResult first;
  status = node.PutKey(request_context, "k", "v1", 0, &first);
  if (!status.ok()) {
    std::cerr << "first put failed: " << status.message << "\n";
    return 1;
  }

  deltafs::replication::PutKeyResult second;
  status = node.PutKey(request_context, "k", "v2", 0, &second);
  if (!status.ok()) {
    std::cerr << "second put failed: " << status.message << "\n";
    return 1;
  }

  if (first.lsn != second.lsn || first.block_id != second.block_id ||
      first.root_version != second.root_version) {
    std::cerr << "idempotency failed: duplicate request_id produced different result\n";
    return 1;
  }

  deltafs::replication::GetKeyResult get_result;
  status = node.GetKey("k", "", 0, &get_result);
  if (!status.ok() || !get_result.found || get_result.value != "v1") {
    std::cerr << "expected stored value v1 after duplicate write, got value=" << get_result.value << "\n";
    return 1;
  }

  std::filesystem::remove_all(base);
  std::cout << "ok\n";
  return 0;
}
