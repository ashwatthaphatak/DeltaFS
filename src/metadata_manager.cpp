#include "metadata_manager.h"
#include "block_store.h"
#include "utils.h"

#include <utility>

MetadataManager::MetadataManager(BlockStore& store) : blockStore(store) {}

void MetadataManager::create_file(const std::string& name, const std::vector<std::string>& blocks) {
    std::lock_guard<std::mutex> lock(mtx);
    FileMetadata metadata;
    metadata.block_ids = blocks;
    std::size_t total_size = 0;
    for (const auto& blockId : blocks) {
        total_size += blockStore.read_block(blockId).size();
    }
    metadata.file_size = total_size;
    files[name] = std::move(metadata);
}

std::vector<std::string> MetadataManager::get_blocks(const std::string& name) {
    std::lock_guard<std::mutex> lock(mtx);
    auto it = files.find(name);
    return it != files.end() ? it->second.block_ids : std::vector<std::string>{};
}

bool MetadataManager::createFile(const std::string& filename) {
    std::lock_guard<std::mutex> lock(mtx);
    if (files.count(filename)) {
        return false; // File already exists
    }
    files.emplace(filename, FileMetadata{});
    return true;
}

bool MetadataManager::readFile(const std::string& filename, std::string& data) {
    std::lock_guard<std::mutex> lock(mtx);
    auto it = files.find(filename);
    if (it == files.end()) {
        return false; // File not found
    }

    std::string buffer;
    buffer.reserve(it->second.file_size);
    for (const auto& blockId : it->second.block_ids) {
        buffer += blockStore.read_block(blockId);
    }
    buffer.resize(it->second.file_size);
    data = std::move(buffer);
    return true;
}

bool MetadataManager::updateFile(const std::string& filename, const std::string& data) {
    std::lock_guard<std::mutex> lock(mtx);
    auto it = files.find(filename);
    if (it == files.end()) {
        return false; // File not found
    }

    it->second.block_ids.clear();
    for (std::size_t offset = 0; offset < data.size(); offset += kDefaultBlockSize) {
        auto chunk = data.substr(offset, kDefaultBlockSize);
        it->second.block_ids.push_back(blockStore.write_block(chunk));
    }
    it->second.file_size = data.size();
    return true;
}

std::vector<std::string> MetadataManager::listFiles() {
    std::lock_guard<std::mutex> lock(mtx);
    std::vector<std::string> filenames;
    filenames.reserve(files.size());
    for (const auto& pair : files) {
        filenames.push_back(pair.first);
    }
    return filenames;
}
