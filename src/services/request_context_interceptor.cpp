#include "deltafs/services/request_context_interceptor.h"

#include <string>

#include "deltafs/common/logging.h"

namespace deltafs {
namespace services {

namespace {

std::string MetadataValue(grpc::experimental::InterceptorBatchMethods* methods, const char* key) {
  const auto* metadata = methods->GetRecvInitialMetadata();
  if (metadata == nullptr) {
    return "";
  }
  auto it = metadata->find(key);
  if (it == metadata->end()) {
    return "";
  }
  return std::string(it->second.data(), it->second.length());
}

class RequestContextInterceptor : public grpc::experimental::Interceptor {
 public:
  void Intercept(grpc::experimental::InterceptorBatchMethods* methods) override {
    if (methods->QueryInterceptionHookPoint(
            grpc::experimental::InterceptionHookPoints::POST_RECV_INITIAL_METADATA)) {
      common::SetLogContext(MetadataValue(methods, "x-request-id"), MetadataValue(methods, "x-node-id"));
    }

    if (methods->QueryInterceptionHookPoint(
            grpc::experimental::InterceptionHookPoints::PRE_SEND_STATUS)) {
      common::ClearLogContext();
    }

    methods->Proceed();
  }
};

class RequestContextInterceptorFactory
    : public grpc::experimental::ServerInterceptorFactoryInterface {
 public:
  grpc::experimental::Interceptor* CreateServerInterceptor(
      grpc::experimental::ServerRpcInfo*) override {
    return new RequestContextInterceptor();
  }
};

}  // namespace

std::vector<std::unique_ptr<grpc::experimental::ServerInterceptorFactoryInterface>>
CreateServerInterceptorFactories() {
  std::vector<std::unique_ptr<grpc::experimental::ServerInterceptorFactoryInterface>> out;
  out.push_back(std::make_unique<RequestContextInterceptorFactory>());
  return out;
}

}  // namespace services
}  // namespace deltafs
