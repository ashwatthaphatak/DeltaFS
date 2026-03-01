#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace deltafs {
namespace common {

int64_t NowUnixMillis();
std::string Trim(const std::string& value);
std::vector<std::string> Split(const std::string& value, char delimiter);

bool EnsureDirectory(const std::string& path, std::string* error_message = nullptr);
std::vector<std::string> ListFilesSorted(const std::string& directory,
                                         const std::string& prefix = "",
                                         const std::string& suffix = "");
bool ReadFileToString(const std::string& path, std::string* out);

std::string GenerateRequestId();

}  // namespace common
}  // namespace deltafs
