#pragma once
#include <string>
#include <unordered_map>
#include <mutex>

using namespace std;

class BlockStore {
    
public:
    string write_block(const string& data);
    string read_block(const string& id);
private:
    unordered_map<string, string> blocks;
    mutex mtx;
};
