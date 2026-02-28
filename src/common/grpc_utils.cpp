#include "deltafs/common/grpc_utils.h"

#include "deltafs/common/utils.h"

namespace deltafs {
namespace common {

void PopulateRequestContext(deltafs::v1::RequestContext* context,
                            const std::string& node_id,
                            const std::string& request_id) {
  context->set_node_id(node_id);
  context->set_request_id(request_id.empty() ? GenerateRequestId() : request_id);
  context->set_client_timestamp_unix_ms(NowUnixMillis());
}

void AddContextMetadata(const deltafs::v1::RequestContext& context,
                        grpc::ClientContext* client_context) {
  client_context->AddMetadata("x-request-id", context.request_id());
  client_context->AddMetadata("x-node-id", context.node_id());
}

bool IsRetryableGrpcStatus(const grpc::Status& status) {
  return status.error_code() == grpc::StatusCode::UNAVAILABLE ||
         status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED;
}

Status FromGrpcStatus(const grpc::Status& status) {
  if (status.ok()) {
    return Status::Ok();
  }
  if (status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED) {
    return Status::Timeout(status.error_message(), true);
  }
  if (status.error_code() == grpc::StatusCode::UNAVAILABLE) {
    return Status::Unavailable(status.error_message(), true);
  }
  if (status.error_code() == grpc::StatusCode::INVALID_ARGUMENT) {
    return Status::InvalidArgument(status.error_message());
  }
  if (status.error_code() == grpc::StatusCode::NOT_FOUND) {
    return Status::NotFound(status.error_message());
  }
  if (status.error_code() == grpc::StatusCode::FAILED_PRECONDITION) {
    return Status::FailedPrecondition(status.error_message());
  }
  return Status::Internal(status.error_message());
}

}  // namespace common
}  // namespace deltafs
