#include "deltafs/common/status.h"

#include "common.pb.h"

namespace deltafs {
namespace common {

Status Status::Ok() { return Status{}; }

Status Status::InvalidArgument(std::string message) {
  Status s;
  s.code = StatusCode::kInvalidArgument;
  s.message = std::move(message);
  return s;
}

Status Status::NotFound(std::string message) {
  Status s;
  s.code = StatusCode::kNotFound;
  s.message = std::move(message);
  return s;
}

Status Status::AlreadyExists(std::string message) {
  Status s;
  s.code = StatusCode::kAlreadyExists;
  s.message = std::move(message);
  return s;
}

Status Status::FailedPrecondition(std::string message) {
  Status s;
  s.code = StatusCode::kFailedPrecondition;
  s.message = std::move(message);
  return s;
}

Status Status::Unavailable(std::string message, bool retryable_arg) {
  Status s;
  s.code = StatusCode::kUnavailable;
  s.retryable = retryable_arg;
  s.message = std::move(message);
  return s;
}

Status Status::Timeout(std::string message, bool retryable_arg) {
  Status s;
  s.code = StatusCode::kTimeout;
  s.retryable = retryable_arg;
  s.message = std::move(message);
  return s;
}

Status Status::Internal(std::string message) {
  Status s;
  s.code = StatusCode::kInternal;
  s.message = std::move(message);
  return s;
}

Status Status::NotLeader(std::string message, std::string leader_id) {
  Status s;
  s.code = StatusCode::kNotLeader;
  s.message = std::move(message);
  s.retryable = true;
  if (!leader_id.empty()) {
    s.details.emplace("leader_id", std::move(leader_id));
  }
  return s;
}

Status Status::WithDetail(std::string key, std::string value) const {
  Status out = *this;
  out.details[std::move(key)] = std::move(value);
  return out;
}

const char* StatusCodeToString(StatusCode code) {
  switch (code) {
    case StatusCode::kOk:
      return "OK";
    case StatusCode::kInvalidArgument:
      return "INVALID_ARGUMENT";
    case StatusCode::kNotFound:
      return "NOT_FOUND";
    case StatusCode::kAlreadyExists:
      return "ALREADY_EXISTS";
    case StatusCode::kFailedPrecondition:
      return "FAILED_PRECONDITION";
    case StatusCode::kUnavailable:
      return "UNAVAILABLE";
    case StatusCode::kTimeout:
      return "TIMEOUT";
    case StatusCode::kInternal:
      return "INTERNAL";
    case StatusCode::kNotLeader:
      return "NOT_LEADER";
  }
  return "INTERNAL";
}

StatusCode StatusCodeFromString(const std::string& value) {
  if (value == "OK") {
    return StatusCode::kOk;
  }
  if (value == "INVALID_ARGUMENT") {
    return StatusCode::kInvalidArgument;
  }
  if (value == "NOT_FOUND") {
    return StatusCode::kNotFound;
  }
  if (value == "ALREADY_EXISTS") {
    return StatusCode::kAlreadyExists;
  }
  if (value == "FAILED_PRECONDITION") {
    return StatusCode::kFailedPrecondition;
  }
  if (value == "UNAVAILABLE") {
    return StatusCode::kUnavailable;
  }
  if (value == "TIMEOUT") {
    return StatusCode::kTimeout;
  }
  if (value == "NOT_LEADER") {
    return StatusCode::kNotLeader;
  }
  return StatusCode::kInternal;
}

void FillErrorStatusProto(const Status& status, deltafs::v1::ErrorStatus* out) {
  out->set_code(StatusCodeToString(status.code));
  out->set_message(status.message);
  out->set_retryable(status.retryable);
  out->clear_details();
  for (const auto& item : status.details) {
    auto* detail = out->add_details();
    detail->set_key(item.first);
    detail->set_value(item.second);
  }
}

Status StatusFromErrorStatusProto(const deltafs::v1::ErrorStatus& proto_status) {
  Status status;
  status.code = StatusCodeFromString(proto_status.code());
  status.message = proto_status.message();
  status.retryable = proto_status.retryable();
  for (const auto& detail : proto_status.details()) {
    status.details[detail.key()] = detail.value();
  }
  return status;
}

}  // namespace common
}  // namespace deltafs
