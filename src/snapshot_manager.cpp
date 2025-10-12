#include "snapshot_manager.h"
#include <iostream>

bool SnapshotManager::createSnapshot(const std::string& snapshotName) {
    std::lock_guard<std::mutex> lock(mtx);
    if (snapshots.count(snapshotName)) {
        return false; // Snapshot already exists
    }
    // For simplicity, we'll just store the snapshot name and timestamp
    snapshots[snapshotName] = "snapshot_data_placeholder";
    return true;
}

bool SnapshotManager::rollbackToSnapshot(const std::string& snapshotName) {
    std::lock_guard<std::mutex> lock(mtx);
    if (snapshots.count(snapshotName)) {
        // In a real implementation, this would restore the file system state
        std::cout << "Rolling back to snapshot: " << snapshotName << std::endl;
        return true;
    }
    return false; // Snapshot not found
}

std::vector<std::string> SnapshotManager::listSnapshots() {
    std::lock_guard<std::mutex> lock(mtx);
    std::vector<std::string> snapshotNames;
    for (const auto& pair : snapshots) {
        snapshotNames.push_back(pair.first);
    }
    return snapshotNames;
}

bool SnapshotManager::deleteSnapshot(const std::string& snapshotName){
    std::lock_guard<std::mutex> lock(mtx);
    if (snapshots.count(snapshotName)) {
        snapshots.erase(snapshotName);
        return true;
    }
    return false; // no snapshot for given name
}