#pragma once
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

class BlockStore;

struct FileMetadata {
    std::vector<std::string> block_ids;
    std::size_t file_size{0};
};

class MetadataManager {
public:
    explicit MetadataManager(BlockStore& store);

    void create_file(const std::string& name, const std::vector<std::string>& blocks);
    std::vector<std::string> get_blocks(const std::string& name);
    
    bool createFile(const std::string& filename);
    bool readFile(const std::string& filename, std::string& data);
    bool updateFile(const std::string& filename, const std::string& data);
    std::vector<std::string> listFiles();
    
private:
    std::unordered_map<std::string, FileMetadata> files;
    std::mutex mtx;
    BlockStore& blockStore;
};
