#include "snapshot_manager.h"

#include "journal_manager.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <unordered_set>
#include <utility>

using namespace std;
namespace fs = std::filesystem;

SnapshotManager::SnapshotManager(string storage_path)
    : next_id(1),
      storage_path(move(storage_path)) {
    load_from_disk();
}

int SnapshotManager::create_snapshot(const string& name, MetadataManager& meta) {
    unordered_map<string, string> snapshot_files;
    vector<string> filenames = meta.listFiles();
    for (const auto& filename : filenames) {
        string data;
        if (meta.readFile(filename, data)) {
            snapshot_files.emplace(filename, move(data));
        }
    }

    lock_guard<mutex> lock(mtx);
    Snapshot snapshot;
    snapshot.id = next_id++;
    snapshot.name = name;
    snapshot.files = move(snapshot_files);
    snapshots.push_back(snapshot);
    save_to_disk_locked();
    return snapshot.id;
}

bool SnapshotManager::rollback(int snapshot_id, MetadataManager& meta, JournalManager& journal) {
    unordered_map<string, string> snapshot_files;
    {
        lock_guard<mutex> lock(mtx);
        auto it = find_if(snapshots.begin(), snapshots.end(),
                          [snapshot_id](const Snapshot& snapshot) {
                              return snapshot.id == snapshot_id;
                          });
        if (it == snapshots.end()) {
            return false;
        }
        snapshot_files = it->files;
    }

    vector<string> current_files = meta.listFiles();
    unordered_set<string> snapshot_file_names;
    snapshot_file_names.reserve(snapshot_files.size());
    for (const auto& entry : snapshot_files) {
        snapshot_file_names.insert(entry.first);
    }

    for (const auto& file : current_files) {
        if (!snapshot_file_names.count(file)) {
            journal.log_operation("delete", file);
            meta.deleteFile(file);
        }
    }

    for (const auto& [file, data] : snapshot_files) {
        bool exists = meta.fileExists(file);
        if (!exists) {
            journal.log_operation("create", file);
            meta.createFile(file);
        }

        journal.log_operation("update", file, data);
        meta.updateFile(file, data);
    }

    return true;
}

vector<Snapshot> SnapshotManager::list_snapshots() const {
    lock_guard<mutex> lock(mtx);
    return snapshots;
}

void SnapshotManager::load_from_disk() {
    lock_guard<mutex> lock(mtx);

    snapshots.clear();
    next_id = 1;

    ifstream in(storage_path);
    if (!in) {
        return;
    }

    string line;
    Snapshot current;
    bool has_current = false;
    int max_id = 0;

    while (getline(in, line)) {
        if (line.empty()) {
            continue;
        }

        istringstream iss(line);
        string tag;
        iss >> tag;
        if (tag == "SNAPSHOT") {
            if (has_current) {
                snapshots.push_back(move(current));
                has_current = false;
            }

            current = Snapshot{};
            if (!(iss >> current.id)) {
                continue;
            }
            if (!(iss >> quoted(current.name))) {
                current.name.clear();
            }
            current.files.clear();
            has_current = true;
            max_id = max(max_id, current.id);
        } else if (tag == "FILE" && has_current) {
            string filename;
            string data;
            if (iss >> quoted(filename) >> quoted(data)) {
                current.files.emplace(move(filename), move(data));
            }
        } else if (tag == "END") {
            if (has_current) {
                snapshots.push_back(move(current));
                has_current = false;
            }
        }
    }

    if (has_current) {
        snapshots.push_back(move(current));
    }

    for (const auto& snapshot : snapshots) {
        max_id = max(max_id, snapshot.id);
    }
    next_id = max_id + 1;
}

void SnapshotManager::save_to_disk_locked() const {
    fs::path path(storage_path);
    if (!path.parent_path().empty()) {
        fs::create_directories(path.parent_path());
    }

    ofstream out(storage_path, ios::trunc);
    if (!out) {
        cerr << "Failed to write snapshots to " << storage_path << endl;
        return;
    }

    for (const auto& snapshot : snapshots) {
        out << "SNAPSHOT " << snapshot.id << ' ' << quoted(snapshot.name) << '\n';
        for (const auto& entry : snapshot.files) {
            out << "FILE " << quoted(entry.first) << ' ' << quoted(entry.second) << '\n';
        }
        out << "END\n";
    }
}
