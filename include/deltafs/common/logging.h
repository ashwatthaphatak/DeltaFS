#pragma once

#include <string>

namespace deltafs {
namespace common {

enum class LogLevel { kDebug, kInfo, kWarn, kError };

void SetLogContext(std::string request_id, std::string node_id);
void ClearLogContext();

class ScopedLogContext {
 public:
  ScopedLogContext(std::string request_id, std::string node_id);
  ~ScopedLogContext();

 private:
  std::string previous_request_id_;
  std::string previous_node_id_;
};

std::string CurrentRequestId();
std::string CurrentNodeId();

void Log(LogLevel level, const std::string& message);

}  // namespace common
}  // namespace deltafs

#define DELTAFS_LOG_DEBUG(msg) ::deltafs::common::Log(::deltafs::common::LogLevel::kDebug, (msg))
#define DELTAFS_LOG_INFO(msg) ::deltafs::common::Log(::deltafs::common::LogLevel::kInfo, (msg))
#define DELTAFS_LOG_WARN(msg) ::deltafs::common::Log(::deltafs::common::LogLevel::kWarn, (msg))
#define DELTAFS_LOG_ERROR(msg) ::deltafs::common::Log(::deltafs::common::LogLevel::kError, (msg))
