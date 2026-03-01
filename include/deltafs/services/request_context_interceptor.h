#pragma once

#include <memory>
#include <vector>

#include <grpcpp/support/server_interceptor.h>

namespace deltafs {
namespace services {

std::vector<std::unique_ptr<grpc::experimental::ServerInterceptorFactoryInterface>>
CreateServerInterceptorFactories();

}  // namespace services
}  // namespace deltafs
