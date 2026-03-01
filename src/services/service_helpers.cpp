#include "deltafs/services/service_helpers.h"

namespace deltafs {
namespace services {

common::Status ValidateRequestContext(const deltafs::v1::RequestContext& context) {
  if (context.request_id().empty()) {
    return common::Status::InvalidArgument("request_id is required");
  }
  if (context.node_id().empty()) {
    return common::Status::InvalidArgument("node_id is required");
  }
  return common::Status::Ok();
}

void SetResponseError(const common::Status& status, deltafs::v1::ErrorStatus* out) {
  common::FillErrorStatusProto(status, out);
}

}  // namespace services
}  // namespace deltafs
