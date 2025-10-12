#include "journal_manager.h"
#include <chrono>

void JournalManager::logOperation(const std::string& operation, const std::string& filename, const std::string& data) {
    std::lock_guard<std::mutex> lock(mtx);
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    
    JournalEntry entry = { operation, filename, data, timestamp };
    journal.push_back(entry);
}

std::vector<JournalEntry> JournalManager::getJournal() {
    std::lock_guard<std::mutex> lock(mtx);
    return journal;
}

void JournalManager::clearJournal() {
    std::lock_guard<std::mutex> lock(mtx);
    journal.clear();
}