#include "deltafs/common/logging.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>

namespace deltafs {
namespace common {

namespace {
thread_local std::string t_request_id;
thread_local std::string t_node_id;
std::mutex g_log_mutex;

const char* LevelToString(LogLevel level) {
  switch (level) {
    case LogLevel::kDebug:
      return "DEBUG";
    case LogLevel::kInfo:
      return "INFO";
    case LogLevel::kWarn:
      return "WARN";
    case LogLevel::kError:
      return "ERROR";
  }
  return "INFO";
}

std::string TimeStampNow() {
  auto now = std::chrono::system_clock::now();
  auto millis =
      std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
  std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tm_value;
#if defined(_WIN32)
  localtime_s(&tm_value, &t);
#else
  localtime_r(&t, &tm_value);
#endif
  std::ostringstream oss;
  oss << std::put_time(&tm_value, "%Y-%m-%d %H:%M:%S") << '.' << std::setw(3) << std::setfill('0')
      << millis;
  return oss.str();
}

}  // namespace

void SetLogContext(std::string request_id, std::string node_id) {
  t_request_id = std::move(request_id);
  t_node_id = std::move(node_id);
}

void ClearLogContext() {
  t_request_id.clear();
  t_node_id.clear();
}

ScopedLogContext::ScopedLogContext(std::string request_id, std::string node_id)
    : previous_request_id_(t_request_id), previous_node_id_(t_node_id) {
  SetLogContext(std::move(request_id), std::move(node_id));
}

ScopedLogContext::~ScopedLogContext() { SetLogContext(previous_request_id_, previous_node_id_); }

std::string CurrentRequestId() { return t_request_id; }

std::string CurrentNodeId() { return t_node_id; }

void Log(LogLevel level, const std::string& message) {
  std::lock_guard<std::mutex> lock(g_log_mutex);
  std::ostringstream line;
  line << TimeStampNow() << " [" << LevelToString(level) << "]"
       << " [tid=" << std::this_thread::get_id() << "]";
  if (!t_node_id.empty()) {
    line << " [node_id=" << t_node_id << ']';
  }
  if (!t_request_id.empty()) {
    line << " [request_id=" << t_request_id << ']';
  }
  line << ' ' << message << '\n';
  std::cerr << line.str();
}

}  // namespace common
}  // namespace deltafs
