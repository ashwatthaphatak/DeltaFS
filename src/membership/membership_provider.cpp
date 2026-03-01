#include "deltafs/membership/membership_provider.h"

#include <sstream>

namespace deltafs {
namespace membership {

K8sMembershipProvider::K8sMembershipProvider(common::NodeConfig config) : config_(std::move(config)) {}

std::vector<common::PeerEndpoint> K8sMembershipProvider::ListPeers() const {
  if (!config_.peers.empty()) {
    return config_.peers;
  }

  std::vector<common::PeerEndpoint> peers;
  peers.reserve(static_cast<size_t>(config_.replicas));

  for (int i = 0; i < config_.replicas; ++i) {
    common::PeerEndpoint peer;
    peer.node_id = config_.statefulset_name + "-" + std::to_string(i);
    std::ostringstream endpoint;
    endpoint << peer.node_id << '.' << config_.headless_service;
    if (!config_.namespace_name.empty()) {
      endpoint << '.' << config_.namespace_name << ".svc.cluster.local";
    }
    endpoint << ':' << config_.grpc_port;
    peer.endpoint = endpoint.str();
    peers.push_back(std::move(peer));
  }

  return peers;
}

std::unique_ptr<MembershipProvider> CreateMembershipProvider(const common::NodeConfig& config,
                                                             common::Status* status) {
  if (config.membership_provider == "compose") {
    if (config.peers.empty()) {
      *status = common::Status::InvalidArgument("compose membership requires peers");
      return nullptr;
    }
    *status = common::Status::Ok();
    return std::make_unique<ComposeMembershipProvider>(config.peers);
  }

  if (config.membership_provider == "k8s") {
    *status = common::Status::Ok();
    return std::make_unique<K8sMembershipProvider>(config);
  }

  if (config.membership_provider == "static") {
    *status = common::Status::Ok();
    return std::make_unique<ComposeMembershipProvider>(config.peers);
  }

  *status = common::Status::InvalidArgument("unknown membership_provider: " +
                                            config.membership_provider);
  return nullptr;
}

}  // namespace membership
}  // namespace deltafs
