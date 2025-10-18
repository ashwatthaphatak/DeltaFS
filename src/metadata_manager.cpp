#include "metadata_manager.h"
#include "block_store.h"
#include "utils.h"
#include <utility>

using namespace std;

MetadataManager::MetadataManager(BlockStore& store) : blockStore(store) {}

bool MetadataManager::createFile(const string& filename) {
    lock_guard<mutex> lock(mtx);
    if (files.count(filename)) {
        return false; // File already exists
    }
    files.emplace(filename, FileMetadata{});
    return true;
}

bool MetadataManager::readFile(const string& filename, string& data) {
    lock_guard<mutex> lock(mtx);
    auto it = files.find(filename);
    if (it == files.end()) {
        return false; // File not found
    }

    string buffer;
    buffer.reserve(it->second.file_size);
    for (const auto& blockId : it->second.block_ids) {
        buffer += blockStore.read_block(blockId);
    }
    buffer.resize(it->second.file_size);
    data = move(buffer);
    return true;
}

bool MetadataManager::updateFile(const string& filename, const string& data) {
    lock_guard<mutex> lock(mtx);
    auto it = files.find(filename);
    if (it == files.end()) {
        return false; // File not found
    }

    it->second.block_ids.clear();
    for (size_t offset = 0; offset < data.size(); offset += kDefaultBlockSize) {
        auto chunk = data.substr(offset, kDefaultBlockSize);
        it->second.block_ids.push_back(blockStore.write_block(chunk));
    }
    it->second.file_size = data.size();
    return true;
}

bool MetadataManager::deleteFile(const string& filename) {
    lock_guard<mutex> lock(mtx);
    return files.erase(filename) > 0;
}

bool MetadataManager::fileExists(const string& filename) const {
    lock_guard<mutex> lock(mtx);
    return files.find(filename) != files.end();
}

vector<string> MetadataManager::listFiles() const {
    lock_guard<mutex> lock(mtx);
    vector<string> filenames;
    filenames.reserve(files.size());
    for (const auto& pair : files) {
        filenames.push_back(pair.first);
    }
    return filenames;
}

unordered_map<string, FileMetadata> MetadataManager::exportFileTable() const {
    lock_guard<mutex> lock(mtx);
    return files;
}

void MetadataManager::importFileTable(const unordered_map<string, FileMetadata>& table) {
    lock_guard<mutex> lock(mtx);
    files = table;
}
