#pragma once

#include "common.pb.h"
#include "deltafs/common/status.h"

namespace deltafs {
namespace services {

common::Status ValidateRequestContext(const deltafs::v1::RequestContext& context);
void SetResponseError(const common::Status& status, deltafs::v1::ErrorStatus* out);

}  // namespace services
}  // namespace deltafs
