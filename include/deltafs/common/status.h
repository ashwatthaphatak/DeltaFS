#pragma once

#include <map>
#include <string>

namespace deltafs {
namespace v1 {
class ErrorStatus;
}  // namespace v1

namespace common {

enum class StatusCode {
  kOk,
  kInvalidArgument,
  kNotFound,
  kAlreadyExists,
  kFailedPrecondition,
  kUnavailable,
  kTimeout,
  kInternal,
  kNotLeader,
};

struct Status {
  StatusCode code = StatusCode::kOk;
  std::string message;
  bool retryable = false;
  std::map<std::string, std::string> details;

  bool ok() const { return code == StatusCode::kOk; }

  static Status Ok();
  static Status InvalidArgument(std::string message);
  static Status NotFound(std::string message);
  static Status AlreadyExists(std::string message);
  static Status FailedPrecondition(std::string message);
  static Status Unavailable(std::string message, bool retryable = true);
  static Status Timeout(std::string message, bool retryable = true);
  static Status Internal(std::string message);
  static Status NotLeader(std::string message, std::string leader_id = "");

  Status WithDetail(std::string key, std::string value) const;
};

const char* StatusCodeToString(StatusCode code);
StatusCode StatusCodeFromString(const std::string& value);

void FillErrorStatusProto(const Status& status, deltafs::v1::ErrorStatus* out);
Status StatusFromErrorStatusProto(const deltafs::v1::ErrorStatus& proto_status);

}  // namespace common
}  // namespace deltafs
