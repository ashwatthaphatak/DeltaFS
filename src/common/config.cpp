#include "deltafs/common/config.h"

#include <fstream>

#include "deltafs/common/utils.h"

namespace deltafs {
namespace common {

namespace {

Status ParsePeerLine(const std::string& line, PeerEndpoint* out) {
  auto value = Trim(line);
  if (value.rfind("- ", 0) == 0) {
    value = Trim(value.substr(2));
  }
  const auto eq = value.find('=');
  if (eq == std::string::npos || eq == 0 || eq + 1 >= value.size()) {
    return Status::InvalidArgument("invalid peer entry: " + line);
  }
  out->node_id = Trim(value.substr(0, eq));
  out->endpoint = Trim(value.substr(eq + 1));
  if (out->node_id.empty() || out->endpoint.empty()) {
    return Status::InvalidArgument("invalid peer entry: " + line);
  }
  return Status::Ok();
}

int ParseInt(const std::string& value, int fallback) {
  try {
    return std::stoi(value);
  } catch (...) {
    return fallback;
  }
}

}  // namespace

Status LoadNodeConfig(const std::string& path, NodeConfig* config) {
  std::ifstream in(path);
  if (!in) {
    return Status::NotFound("config file not found: " + path);
  }

  NodeConfig parsed;
  std::string line;
  bool parsing_peers = false;

  while (std::getline(in, line)) {
    auto trimmed = Trim(line);
    if (trimmed.empty() || trimmed.rfind('#', 0) == 0) {
      continue;
    }

    if (parsing_peers && trimmed.rfind("- ", 0) == 0) {
      PeerEndpoint peer;
      auto status = ParsePeerLine(trimmed, &peer);
      if (!status.ok()) {
        return status;
      }
      parsed.peers.push_back(std::move(peer));
      continue;
    }

    const auto colon = trimmed.find(':');
    if (colon == std::string::npos) {
      continue;
    }

    const auto key = Trim(trimmed.substr(0, colon));
    const auto value = Trim(trimmed.substr(colon + 1));

    parsing_peers = false;

    if (key == "node_id") {
      parsed.node_id = value;
    } else if (key == "listen_address") {
      parsed.listen_address = value;
    } else if (key == "advertised_address") {
      parsed.advertised_address = value;
    } else if (key == "leader_id") {
      parsed.leader_id = value;
    } else if (key == "ack_policy") {
      auto status = ParseAckPolicy(value, &parsed.ack_policy);
      if (!status.ok()) {
        return status;
      }
    } else if (key == "data_dir") {
      parsed.data_dir = value;
    } else if (key == "membership_provider") {
      parsed.membership_provider = value;
    } else if (key == "rpc_timeout_ms") {
      parsed.rpc_timeout_ms = ParseInt(value, parsed.rpc_timeout_ms);
    } else if (key == "max_retries") {
      parsed.max_retries = ParseInt(value, parsed.max_retries);
    } else if (key == "statefulset_name") {
      parsed.statefulset_name = value;
    } else if (key == "headless_service") {
      parsed.headless_service = value;
    } else if (key == "namespace_name") {
      parsed.namespace_name = value;
    } else if (key == "replicas") {
      parsed.replicas = ParseInt(value, parsed.replicas);
    } else if (key == "grpc_port") {
      parsed.grpc_port = ParseInt(value, parsed.grpc_port);
    } else if (key == "peers") {
      parsing_peers = true;
    }
  }

  if (parsed.node_id.empty()) {
    return Status::InvalidArgument("node_id is required in config");
  }
  if (parsed.advertised_address.empty()) {
    parsed.advertised_address = parsed.listen_address;
  }

  if (parsed.membership_provider == "compose" && parsed.peers.empty()) {
    return Status::InvalidArgument("compose membership requires peers list");
  }

  *config = std::move(parsed);
  return Status::Ok();
}

std::string AckPolicyToString(AckPolicy policy) {
  switch (policy) {
    case AckPolicy::kOne:
      return "one";
    case AckPolicy::kMajority:
      return "majority";
    case AckPolicy::kAll:
      return "all";
  }
  return "majority";
}

Status ParseAckPolicy(const std::string& value, AckPolicy* policy) {
  if (value == "one") {
    *policy = AckPolicy::kOne;
    return Status::Ok();
  }
  if (value == "majority") {
    *policy = AckPolicy::kMajority;
    return Status::Ok();
  }
  if (value == "all") {
    *policy = AckPolicy::kAll;
    return Status::Ok();
  }
  return Status::InvalidArgument("invalid ack_policy: " + value);
}

size_t RequiredAckCount(AckPolicy policy, size_t cluster_size) {
  if (cluster_size == 0) {
    return 0;
  }
  switch (policy) {
    case AckPolicy::kOne:
      return 1;
    case AckPolicy::kMajority:
      return (cluster_size / 2) + 1;
    case AckPolicy::kAll:
      return cluster_size;
  }
  return (cluster_size / 2) + 1;
}

bool IsLeader(const NodeConfig& config) { return config.node_id == config.leader_id; }

}  // namespace common
}  // namespace deltafs
