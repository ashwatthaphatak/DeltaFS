#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

#include "deltafs/common/status.h"

namespace deltafs {
namespace metadata {

class MetadataManager {
 public:
  explicit MetadataManager(std::string metadata_dir);

  common::Status Initialize();

  common::Status ApplyPut(const std::string& key,
                          const std::string& block_id,
                          uint64_t root_version_context,
                          uint64_t* new_root_version);

  common::Status ApplyReplicatedPut(const std::string& key,
                                    const std::string& block_id,
                                    uint64_t base_root_version,
                                    uint64_t new_root_version);

  common::Status GetBlockForKey(const std::string& key,
                                uint64_t root_version,
                                std::string* block_id,
                                bool* found) const;

  common::Status CommitRoot(uint64_t root_version);

  uint64_t committed_root() const;
  uint64_t latest_root() const;
  const std::string& metadata_dir() const { return metadata_dir_; }

 private:
  common::Status PersistRootLocked(uint64_t root_version,
                                   const std::unordered_map<std::string, std::string>& map);
  common::Status PersistCommittedRootLocked();
  common::Status LoadRootFile(const std::string& path,
                              uint64_t root_version,
                              std::unordered_map<std::string, std::string>* out);

  std::string metadata_dir_;
  std::string roots_dir_;
  std::string committed_root_path_;

  mutable std::mutex mutex_;
  uint64_t committed_root_ = 0;
  uint64_t latest_root_ = 0;
  std::unordered_map<uint64_t, std::unordered_map<std::string, std::string>> roots_;
};

}  // namespace metadata
}  // namespace deltafs
