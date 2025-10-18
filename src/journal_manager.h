#pragma once
#include <string>
#include <vector>
#include <mutex>

using namespace std;

class BlockStore;
class MetadataManager;

struct JournalEntry {
    string operation;
    string filename;
    string data;
    long timestamp;
};

class JournalManager {
public:
    explicit JournalManager(string log_path = "data/journal.log");

    void log_operation(const string& operation, const string& filename, const string& data = "");
    void replay(MetadataManager& metadata, BlockStore& block_store);
    vector<JournalEntry> getJournal() const;
    void clearJournal();

private:
    string log_path;
    vector<JournalEntry> journal;
    mutable mutex mtx;
};
