#include <filesystem>
#include <iostream>

#include "deltafs/common/utils.h"
#include "deltafs/metadata/metadata_manager.h"
#include "deltafs/snapshot/snapshot_manager.h"

int main() {
  const std::string base = "/tmp/deltafs_test_snapshot_" + deltafs::common::GenerateRequestId();
  std::filesystem::create_directories(base);

  deltafs::metadata::MetadataManager metadata(base + "/metadata");
  auto status = metadata.Initialize();
  if (!status.ok()) {
    std::cerr << "metadata init failed: " << status.message << "\n";
    return 1;
  }

  status = metadata.ApplyReplicatedPut("k", "block-old", 0, 1);
  if (!status.ok()) {
    std::cerr << "apply root1 failed: " << status.message << "\n";
    return 1;
  }
  status = metadata.CommitRoot(1);
  if (!status.ok()) {
    std::cerr << "commit root1 failed: " << status.message << "\n";
    return 1;
  }

  deltafs::snapshot::SnapshotManager snapshots(base + "/snapshots");
  status = snapshots.Initialize();
  if (!status.ok()) {
    std::cerr << "snapshot init failed: " << status.message << "\n";
    return 1;
  }

  std::string snapshot_id;
  int64_t created_ms = 0;
  status = snapshots.CreateSnapshot("S1", metadata.committed_root(), &snapshot_id, &created_ms);
  if (!status.ok()) {
    std::cerr << "create snapshot failed: " << status.message << "\n";
    return 1;
  }

  status = metadata.ApplyReplicatedPut("k", "block-new", 1, 2);
  if (!status.ok()) {
    std::cerr << "apply root2 failed: " << status.message << "\n";
    return 1;
  }
  status = metadata.CommitRoot(2);
  if (!status.ok()) {
    std::cerr << "commit root2 failed: " << status.message << "\n";
    return 1;
  }

  uint64_t snapshot_root = 0;
  status = snapshots.GetSnapshotRoot(snapshot_id, &snapshot_root);
  if (!status.ok()) {
    std::cerr << "GetSnapshotRoot failed: " << status.message << "\n";
    return 1;
  }
  if (snapshot_root != 1) {
    std::cerr << "expected snapshot root=1 got " << snapshot_root << "\n";
    return 1;
  }

  bool found = false;
  std::string block_id;
  status = metadata.GetBlockForKey("k", snapshot_root, &block_id, &found);
  if (!status.ok() || !found || block_id != "block-old") {
    std::cerr << "snapshot root did not keep old block\n";
    return 1;
  }

  status = metadata.GetBlockForKey("k", 2, &block_id, &found);
  if (!status.ok() || !found || block_id != "block-new") {
    std::cerr << "latest root missing new block\n";
    return 1;
  }

  std::filesystem::remove_all(base);
  std::cout << "ok\n";
  return 0;
}
