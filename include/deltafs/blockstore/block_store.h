#pragma once

#include <cstdint>
#include <mutex>
#include <string>

#include "deltafs/common/status.h"

namespace deltafs {
namespace blockstore {

class BlockStore {
 public:
  explicit BlockStore(std::string blocks_dir);

  common::Status Initialize();

  common::Status PutBlock(const std::string& data, std::string* block_id);
  common::Status PutBlockWithId(const std::string& block_id, const std::string& data);
  common::Status GetBlock(const std::string& block_id, std::string* data, bool* found) const;

  const std::string& blocks_dir() const { return blocks_dir_; }

 private:
  common::Status WriteBlockFile(const std::string& block_id, const std::string& data, bool* created);

  std::string blocks_dir_;
  mutable std::mutex mutex_;
  uint64_t next_counter_ = 1;
};

}  // namespace blockstore
}  // namespace deltafs
