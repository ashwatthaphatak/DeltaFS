#include "journal_manager.h"

#include "block_store.h"
#include "metadata_manager.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <utility>

using namespace std;
namespace fs = std::filesystem;

JournalManager::JournalManager(string log_path) : log_path(move(log_path)) {
    fs::path path(this->log_path);
    if (!path.parent_path().empty()) {
        fs::create_directories(path.parent_path());
    }
}

void JournalManager::log_operation(const string& operation, const string& filename, const string& data) {
    lock_guard<mutex> lock(mtx);
    auto now = chrono::system_clock::now();
    auto timestamp = chrono::duration_cast<chrono::seconds>(now.time_since_epoch()).count();

    fs::path path(log_path);
    if (!path.parent_path().empty()) {
        fs::create_directories(path.parent_path());
    }

    ofstream out(log_path, ios::app);
    if (!out) {
        cerr << "Failed to open journal log at " << log_path << endl;
    } else {
        out << quoted(operation) << ' '
            << quoted(filename) << ' '
            << quoted(data) << '\n';
        out.flush();
    }

    journal.push_back({ operation, filename, data, timestamp });
}

void JournalManager::replay(MetadataManager& metadata, BlockStore& block_store) {
    lock_guard<mutex> lock(mtx);

    journal.clear();

    ifstream in(log_path);
    if (!in) {
        return;
    }

    string operation;
    string filename;
    string data;

    while (in >> quoted(operation) >> quoted(filename) >> quoted(data)) {
        JournalEntry entry{ operation, filename, data, 0 };
        journal.push_back(entry);

        if (operation == "create") {
            metadata.createFile(filename);
        } else if (operation == "update") {
            if (!metadata.fileExists(filename)) {
                metadata.createFile(filename);
            }
            metadata.updateFile(filename, data);
        } else if (operation == "delete") {
            metadata.deleteFile(filename);
        }
    }

    (void)block_store;
}

vector<JournalEntry> JournalManager::getJournal() const {
    lock_guard<mutex> lock(mtx);
    return journal;
}

void JournalManager::clearJournal() {
    lock_guard<mutex> lock(mtx);
    journal.clear();
    ofstream out(log_path, ios::trunc);
    (void)out;
}
