#include <iostream>
#include "block_store.h"
#include "metadata_manager.h"
#include "snapshot_manager.h"
#include "journal_manager.h"

int main() {
    std::cout << "DeltaFS initialized.\n";
    std::cout << "Commands: create, read, update, snapshot, rollback, list\n";
    
    // TODO: Implement interactive CLI loop here.
    return 0;
}