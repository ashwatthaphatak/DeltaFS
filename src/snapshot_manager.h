#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

class SnapshotManager {
public:
    bool createSnapshot(const std::string& snapshotName);
    bool rollbackToSnapshot(const std::string& snapshotName);
    std::vector<std::string> listSnapshots();
    bool deleteSnapshot(const std::string& snapshotName);
private:
    std::unordered_map<std::string, std::string> snapshots; // Simple snapshot storage
    std::mutex mtx;
};