#pragma once

#include <chrono>
#include <string>

#include <grpcpp/grpcpp.h>

#include "common.pb.h"
#include "deltafs/common/status.h"

namespace deltafs {
namespace common {

void PopulateRequestContext(deltafs::v1::RequestContext* context,
                            const std::string& node_id,
                            const std::string& request_id);

void AddContextMetadata(const deltafs::v1::RequestContext& context,
                        grpc::ClientContext* client_context);

bool IsRetryableGrpcStatus(const grpc::Status& status);
Status FromGrpcStatus(const grpc::Status& status);

}  // namespace common
}  // namespace deltafs
