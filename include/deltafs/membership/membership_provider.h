#pragma once

#include <memory>
#include <string>
#include <vector>

#include "deltafs/common/config.h"
#include "deltafs/common/status.h"

namespace deltafs {
namespace membership {

class MembershipProvider {
 public:
  virtual ~MembershipProvider() = default;
  virtual std::vector<common::PeerEndpoint> ListPeers() const = 0;
};

class ComposeMembershipProvider : public MembershipProvider {
 public:
  explicit ComposeMembershipProvider(std::vector<common::PeerEndpoint> peers)
      : peers_(std::move(peers)) {}

  std::vector<common::PeerEndpoint> ListPeers() const override { return peers_; }

 private:
  std::vector<common::PeerEndpoint> peers_;
};

class K8sMembershipProvider : public MembershipProvider {
 public:
  explicit K8sMembershipProvider(common::NodeConfig config);

  std::vector<common::PeerEndpoint> ListPeers() const override;

 private:
  common::NodeConfig config_;
};

std::unique_ptr<MembershipProvider> CreateMembershipProvider(const common::NodeConfig& config,
                                                             common::Status* status);

}  // namespace membership
}  // namespace deltafs
