#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

#include "deltafs/common/status.h"

namespace deltafs {
namespace common {

struct DedupeRecord {
  std::string request_id;
  std::string operation;
  bool success = true;
  uint64_t lsn = 0;
  uint64_t root_version = 0;
  std::string block_id;
  std::string snapshot_id;
  int64_t timestamp_unix_ms = 0;
};

class DedupeStore {
 public:
  explicit DedupeStore(std::string dir_path);

  Status Load();
  bool Lookup(const std::string& request_id, DedupeRecord* out) const;
  Status Remember(const DedupeRecord& record);
  size_t Size() const;

 private:
  std::string dir_path_;
  std::string records_path_;
  mutable std::mutex mutex_;
  std::unordered_map<std::string, DedupeRecord> records_;
};

}  // namespace common
}  // namespace deltafs
