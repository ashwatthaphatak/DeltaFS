#include "deltafs/common/utils.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

#include <uuid/uuid.h>

namespace deltafs {
namespace common {

int64_t NowUnixMillis() {
  using Clock = std::chrono::system_clock;
  return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
}

std::string Trim(const std::string& value) {
  const auto begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return "";
  }
  const auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1);
}

std::vector<std::string> Split(const std::string& value, char delimiter) {
  std::vector<std::string> parts;
  std::stringstream ss(value);
  std::string item;
  while (std::getline(ss, item, delimiter)) {
    parts.push_back(item);
  }
  return parts;
}

bool EnsureDirectory(const std::string& path, std::string* error_message) {
  try {
    std::filesystem::create_directories(path);
    return true;
  } catch (const std::exception& ex) {
    if (error_message != nullptr) {
      *error_message = ex.what();
    }
    return false;
  }
}

std::vector<std::string> ListFilesSorted(const std::string& directory,
                                         const std::string& prefix,
                                         const std::string& suffix) {
  std::vector<std::string> paths;
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
    if (ec) {
      break;
    }
    if (!entry.is_regular_file()) {
      continue;
    }
    const auto filename = entry.path().filename().string();
    if (!prefix.empty() && filename.rfind(prefix, 0) != 0) {
      continue;
    }
    if (!suffix.empty()) {
      if (filename.size() < suffix.size() ||
          filename.substr(filename.size() - suffix.size()) != suffix) {
        continue;
      }
    }
    paths.push_back(entry.path().string());
  }
  std::sort(paths.begin(), paths.end());
  return paths;
}

bool ReadFileToString(const std::string& path, std::string* out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return false;
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  *out = ss.str();
  return true;
}

std::string GenerateRequestId() {
  uuid_t uuid;
  char out[37];
  uuid_generate(uuid);
  uuid_unparse(uuid, out);
  return std::string(out);
}

}  // namespace common
}  // namespace deltafs
