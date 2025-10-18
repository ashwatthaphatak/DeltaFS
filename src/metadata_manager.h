#pragma once
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

using namespace std;

class BlockStore;

struct FileMetadata {
    vector<string> block_ids;
    size_t file_size{0};
};

class MetadataManager {
public:
    explicit MetadataManager(BlockStore& store);

    bool createFile(const string& filename);
    bool readFile(const string& filename, string& data);
    bool updateFile(const string& filename, const string& data);
    bool deleteFile(const string& filename);
    bool fileExists(const string& filename) const;
    vector<string> listFiles() const;
    unordered_map<string, FileMetadata> exportFileTable() const;
    void importFileTable(const unordered_map<string, FileMetadata>& table);
    
private:
    unordered_map<string, FileMetadata> files;
    mutable mutex mtx;
    BlockStore& blockStore;
};
