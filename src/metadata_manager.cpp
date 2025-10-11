#include "metadata_manager.h"

void MetadataManager::create_file(const std::string& name, const std::vector<std::string>& blocks) {
    std::lock_guard<std::mutex> lock(mtx);
    files[name] = { blocks };
}

std::vector<std::string> MetadataManager::get_blocks(const std::string& name) {
    std::lock_guard<std::mutex> lock(mtx);
    return files.count(name) ? files[name].block_ids : std::vector<std::string>{};
}