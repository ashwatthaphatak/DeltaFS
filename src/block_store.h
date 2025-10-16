#pragma once
#include <string>
#include <unordered_map>
#include <mutex>

class BlockStore {
    
public:
    std::string write_block(const std::string& data);
    std::string read_block(const std::string& id);
private:
    std::unordered_map<std::string, std::string> blocks;
    std::mutex mtx;
};