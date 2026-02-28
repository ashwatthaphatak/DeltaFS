#include "deltafs/metadata/metadata_manager.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#include "deltafs/common/utils.h"

namespace deltafs {
namespace metadata {

MetadataManager::MetadataManager(std::string metadata_dir)
    : metadata_dir_(std::move(metadata_dir)),
      roots_dir_(metadata_dir_ + "/roots"),
      committed_root_path_(metadata_dir_ + "/committed_root") {}

common::Status MetadataManager::Initialize() {
  std::string error;
  if (!common::EnsureDirectory(metadata_dir_, &error)) {
    return common::Status::Internal("failed to create metadata dir: " + error);
  }
  if (!common::EnsureDirectory(roots_dir_, &error)) {
    return common::Status::Internal("failed to create metadata roots dir: " + error);
  }

  std::lock_guard<std::mutex> lock(mutex_);
  roots_.clear();
  latest_root_ = 0;
  committed_root_ = 0;

  for (const auto& path : common::ListFilesSorted(roots_dir_, "root_", ".meta")) {
    const auto filename = std::filesystem::path(path).filename().string();
    const auto left = filename.find('_');
    const auto right = filename.rfind(".meta");
    if (left == std::string::npos || right == std::string::npos || right <= left + 1) {
      continue;
    }

    uint64_t root_version = 0;
    try {
      root_version = std::stoull(filename.substr(left + 1, right - left - 1));
    } catch (...) {
      continue;
    }

    std::unordered_map<std::string, std::string> root_map;
    auto status = LoadRootFile(path, root_version, &root_map);
    if (!status.ok()) {
      return status;
    }
    roots_[root_version] = std::move(root_map);
    if (root_version > latest_root_) {
      latest_root_ = root_version;
    }
  }

  if (roots_.empty()) {
    roots_[0] = {};
    latest_root_ = 0;
    committed_root_ = 0;
    auto status = PersistRootLocked(0, roots_[0]);
    if (!status.ok()) {
      return status;
    }
    status = PersistCommittedRootLocked();
    if (!status.ok()) {
      return status;
    }
    return common::Status::Ok();
  }

  std::ifstream committed_in(committed_root_path_);
  if (committed_in) {
    committed_in >> committed_root_;
  } else {
    committed_root_ = latest_root_;
    auto status = PersistCommittedRootLocked();
    if (!status.ok()) {
      return status;
    }
  }

  if (roots_.find(committed_root_) == roots_.end()) {
    return common::Status::Internal("committed root not found: " + std::to_string(committed_root_));
  }

  return common::Status::Ok();
}

common::Status MetadataManager::LoadRootFile(const std::string& path,
                                             uint64_t root_version,
                                             std::unordered_map<std::string, std::string>* out) {
  (void)root_version;
  std::ifstream in(path);
  if (!in) {
    return common::Status::Internal("failed to open root file: " + path);
  }

  std::unordered_map<std::string, std::string> result;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) {
      continue;
    }
    std::istringstream iss(line);
    std::string key;
    std::string block_id;
    if (!(iss >> std::quoted(key) >> std::quoted(block_id))) {
      continue;
    }
    result[key] = block_id;
  }
  *out = std::move(result);
  return common::Status::Ok();
}

common::Status MetadataManager::PersistRootLocked(
    uint64_t root_version,
    const std::unordered_map<std::string, std::string>& map) {
  const std::string path = roots_dir_ + "/root_" + std::to_string(root_version) + ".meta";
  std::ofstream out(path, std::ios::trunc);
  if (!out) {
    return common::Status::Internal("failed to write root map: " + path);
  }

  for (const auto& kv : map) {
    out << std::quoted(kv.first) << ' ' << std::quoted(kv.second) << '\n';
  }
  out.flush();
  if (!out) {
    return common::Status::Internal("failed to flush root map: " + path);
  }
  return common::Status::Ok();
}

common::Status MetadataManager::PersistCommittedRootLocked() {
  std::ofstream out(committed_root_path_, std::ios::trunc);
  if (!out) {
    return common::Status::Internal("failed to write committed_root");
  }
  out << committed_root_ << '\n';
  out.flush();
  if (!out) {
    return common::Status::Internal("failed to flush committed_root");
  }
  return common::Status::Ok();
}

common::Status MetadataManager::ApplyPut(const std::string& key,
                                         const std::string& block_id,
                                         uint64_t root_version_context,
                                         uint64_t* new_root_version) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (root_version_context != 0 && root_version_context != committed_root_) {
    return common::Status::FailedPrecondition("root version context mismatch");
  }

  const auto& base = roots_[committed_root_];
  auto updated = base;
  updated[key] = block_id;

  const uint64_t next_root = latest_root_ + 1;
  auto status = PersistRootLocked(next_root, updated);
  if (!status.ok()) {
    return status;
  }

  roots_[next_root] = std::move(updated);
  latest_root_ = next_root;
  *new_root_version = next_root;
  return common::Status::Ok();
}

common::Status MetadataManager::ApplyReplicatedPut(const std::string& key,
                                                   const std::string& block_id,
                                                   uint64_t base_root_version,
                                                   uint64_t new_root_version) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (roots_.find(base_root_version) == roots_.end()) {
    return common::Status::FailedPrecondition("replicated base root missing");
  }
  if (new_root_version <= base_root_version) {
    return common::Status::InvalidArgument("new root version must be > base root version");
  }

  auto updated = roots_[base_root_version];
  updated[key] = block_id;
  auto status = PersistRootLocked(new_root_version, updated);
  if (!status.ok()) {
    return status;
  }

  roots_[new_root_version] = std::move(updated);
  if (new_root_version > latest_root_) {
    latest_root_ = new_root_version;
  }
  return common::Status::Ok();
}

common::Status MetadataManager::GetBlockForKey(const std::string& key,
                                               uint64_t root_version,
                                               std::string* block_id,
                                               bool* found) const {
  std::lock_guard<std::mutex> lock(mutex_);

  const uint64_t effective_root = (root_version == 0) ? committed_root_ : root_version;
  const auto root_it = roots_.find(effective_root);
  if (root_it == roots_.end()) {
    return common::Status::NotFound("root version not found: " + std::to_string(effective_root));
  }

  const auto key_it = root_it->second.find(key);
  if (key_it == root_it->second.end()) {
    *found = false;
    block_id->clear();
    return common::Status::Ok();
  }

  *found = true;
  *block_id = key_it->second;
  return common::Status::Ok();
}

common::Status MetadataManager::CommitRoot(uint64_t root_version) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (roots_.find(root_version) == roots_.end()) {
    return common::Status::NotFound("root version not found");
  }
  committed_root_ = root_version;
  return PersistCommittedRootLocked();
}

uint64_t MetadataManager::committed_root() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return committed_root_;
}

uint64_t MetadataManager::latest_root() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return latest_root_;
}

}  // namespace metadata
}  // namespace deltafs
