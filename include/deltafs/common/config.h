#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "deltafs/common/status.h"

namespace deltafs {
namespace common {

struct PeerEndpoint {
  std::string node_id;
  std::string endpoint;
};

enum class AckPolicy { kOne, kMajority, kAll };

struct NodeConfig {
  std::string node_id;
  std::string listen_address = "0.0.0.0:50051";
  std::string advertised_address;
  std::string leader_id = "node0";
  AckPolicy ack_policy = AckPolicy::kMajority;
  std::string data_dir = "/var/lib/deltafs";
  std::string membership_provider = "compose";

  std::vector<PeerEndpoint> peers;

  int rpc_timeout_ms = 1500;
  int max_retries = 2;

  std::string statefulset_name = "deltafs";
  std::string headless_service = "deltafs-headless";
  std::string namespace_name = "default";
  int replicas = 3;
  int grpc_port = 50051;
};

Status LoadNodeConfig(const std::string& path, NodeConfig* config);
std::string AckPolicyToString(AckPolicy policy);
Status ParseAckPolicy(const std::string& value, AckPolicy* policy);
size_t RequiredAckCount(AckPolicy policy, size_t cluster_size);
bool IsLeader(const NodeConfig& config);

}  // namespace common
}  // namespace deltafs
