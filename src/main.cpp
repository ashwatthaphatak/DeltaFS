#include <iostream>
#include <string>
#include <vector>
#include "block_store.h"
#include "metadata_manager.h"
#include "snapshot_manager.h"
#include "journal_manager.h"
using namespace std;

int main() {
    BlockStore blockstore;
    MetadataManager metadataManager(blockstore);
    SnapshotManager snapshotManager;
    JournalManager journalManager;

    cout << "DeltaFS initialized.\n";
    cout << "Commands: files, snapshots, exit\n";
    
    while (true) {
        string command;
        cout << "> ";
        cin >> command;
        if (command == "exit") {
            break;
        }
        if (command == "files") {
            string operation;
            cout << "Enter your operation: create, read, update, list\n";
            cin >> operation;
            if (operation == "create") {
                string filename;
                cout << "Enter filename: ";
                cin >> filename;
                if (metadataManager.createFile(filename)) {
                    cout << "File created: " << filename << endl;
                } else {
                    cout << "Error creating file: " << filename << endl;
                }
            }
            else if (operation == "read") {
                string filename;
                cout << "Enter filename: ";
                cin >> filename;
                string data;
                if (metadataManager.readFile(filename, data)) {
                    cout << "File content: " << data << endl;
                } else {
                    cout << "Error reading file: " << filename << endl;
                }
            }
            else if (operation == "update") {
                string filename, data;
                cout << "Enter filename: ";
                cin >> filename;
                cout << "Enter new data: ";
                cin.ignore(); // to ignore the newline character left in the buffer
                getline(cin, data);
                if (metadataManager.updateFile(filename, data)) {
                    cout << "File updated: " << filename << endl;
                } else {
                    cout << "Error updating file: " << filename << endl;
                }
            }
            else if (operation == "list") {
                vector<string> files = metadataManager.listFiles();
                cout << "Files:\n";
                for (const auto& file : files) {
                    cout << " - " << file << endl;
                }
            } else {
                cout << "Invalid operation: " << operation << endl;
            }
        }
        if (command == "snapshot"){
            string snapshotName;
            string operation;
            cout << "Enter operation (create/rollback/delete/list): ";
            cin >> operation;
            if (operation == "create") {
                cout << "Enter snapshot name: ";
                cin >> snapshotName;
                if (snapshotManager.createSnapshot(snapshotName)) {
                    cout << "Snapshot created: " << snapshotName << endl;
                } else {
                    cout << "Error creating snapshot: " << snapshotName << endl;
                }
            } else if (operation == "rollback") {
                cout << "Enter snapshot name: ";
                cin >> snapshotName;
                if (snapshotManager.rollbackToSnapshot(snapshotName)) {
                    cout << "Rolled back to snapshot: " << snapshotName << endl;
                } else {
                    cout << "Error rolling back to snapshot: " << snapshotName << endl;
                }
            } else if (operation == "delete") {
                cout << "Enter snapshot name: ";
                cin >> snapshotName;
                if (snapshotManager.deleteSnapshot(snapshotName)) {
                    cout << "Snapshot deleted: " << snapshotName << endl;
                } else {
                    cout << "Error deleting snapshot: " << snapshotName << endl;
                }
            } else if (operation == "list"){
                vector<string> snapshots = snapshotManager.listSnapshots();
                cout << "Snapshots:\n";
                for (const auto& snapshot : snapshots){
                    cout << " - " << snapshot << endl;
                }
            } else {
                cout << "Invalid operation: " << operation << endl;
            }
        } 
        else if (command != "files" && command != "snapshot" && command != "exit") {
                cout << "Invalid command: " << command << endl;
        }
    }
}
