#pragma once
#include <string>
#include <vector>
#include <mutex>

struct JournalEntry {
    std::string operation;
    std::string filename;
    std::string data;
    long timestamp;
};

class JournalManager {
public:
    void logOperation(const std::string& operation, const std::string& filename, const std::string& data = "");
    std::vector<JournalEntry> getJournal();
    void clearJournal();
    
private:
    std::vector<JournalEntry> journal;
    std::mutex mtx;
};