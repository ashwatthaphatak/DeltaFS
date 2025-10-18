#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

#include "metadata_manager.h"

using namespace std;

class JournalManager;

struct Snapshot {
    int id;
    string name;
    unordered_map<string, string> files;
};

class SnapshotManager {
public:
    explicit SnapshotManager(string storage_path = "data/snapshots.log");

    int create_snapshot(const string& name, MetadataManager& meta);
    bool rollback(int snapshot_id, MetadataManager& meta, JournalManager& journal);
    vector<Snapshot> list_snapshots() const;

private:
    void load_from_disk();
    void save_to_disk_locked() const;

    vector<Snapshot> snapshots;
    mutable mutex mtx;
    int next_id;
    string storage_path;
};
