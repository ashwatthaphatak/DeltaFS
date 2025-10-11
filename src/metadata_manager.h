#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

struct FileMetadata {
    std::vector<std::string> block_ids;
};

class MetadataManager {
public:
    void create_file(const std::string& name, const std::vector<std::string>& blocks);
    std::vector<std::string> get_blocks(const std::string& name);
private:
    std::unordered_map<std::string, FileMetadata> files;
    std::mutex mtx;
};