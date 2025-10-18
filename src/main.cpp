#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "block_store.h"
#include "journal_manager.h"
#include "metadata_manager.h"
#include "snapshot_manager.h"

using namespace std;

namespace {

void print_help() {
    cout << "Commands:\n"
         << "  create <filename>\n"
         << "  read <filename>\n"
         << "  update <filename> <data>\n"
         << "  delete <filename>\n"
         << "  list\n"
         << "  snapshot create <name>\n"
         << "  snapshot rollback <id>\n"
         << "  snapshot list\n"
         << "  help\n"
         << "  exit\n";
}

bool parse_snapshot_id(const string& token, int& snapshot_id) {
    try {
        size_t processed = 0;
        snapshot_id = stoi(token, &processed);
        return processed == token.size();
    } catch (const invalid_argument&) {
        return false;
    } catch (const out_of_range&) {
        return false;
    }
}

}  // namespace

int main() {
    BlockStore blockstore;
    MetadataManager metadataManager(blockstore);
    JournalManager journalManager;
    SnapshotManager snapshotManager;

    journalManager.replay(metadataManager, blockstore);

    cout << "DeltaFS initialized. Type 'help' for available commands.\n";

    string line;
    while (true) {
        cout << "> ";
        if (!getline(cin, line)) {
            break;
        }

        istringstream iss(line);
        string command;
        iss >> command;

        if (command.empty()) {
            continue;
        }

        if (command == "exit") {
            cout << "Exiting DeltaFS.\n";
            break;
        }

        if (command == "help") {
            print_help();
            continue;
        }

        if (command == "create") {
            string filename;
            if (!(iss >> filename)) {
                cout << "Usage: create <filename>\n";
                continue;
            }
            if (metadataManager.fileExists(filename)) {
                cout << "File already exists: " << filename << endl;
                continue;
            }

            journalManager.log_operation("create", filename);
            if (metadataManager.createFile(filename)) {
                cout << "File created: " << filename << endl;
            } else {
                cout << "Error creating file: " << filename << endl;
            }
            continue;
        }

        if (command == "read") {
            string filename;
            if (!(iss >> filename)) {
                cout << "Usage: read <filename>\n";
                continue;
            }

            string data;
            if (metadataManager.readFile(filename, data)) {
                cout << "File content: " << data << endl;
            } else {
                cout << "Error reading file: " << filename << endl;
            }
            continue;
        }

        if (command == "update") {
            string filename;
            if (!(iss >> filename)) {
                cout << "Usage: update <filename> <data>\n";
                continue;
            }

            if (!metadataManager.fileExists(filename)) {
                cout << "File not found: " << filename << endl;
                continue;
            }

            string data;
            getline(iss >> ws, data);
            if (!iss && data.empty()) {
                cout << "Usage: update <filename> <data>\n";
                continue;
            }

            journalManager.log_operation("update", filename, data);
            if (metadataManager.updateFile(filename, data)) {
                cout << "File updated: " << filename << endl;
            } else {
                cout << "Error updating file: " << filename << endl;
            }
            continue;
        }

        if (command == "delete") {
            string filename;
            if (!(iss >> filename)) {
                cout << "Usage: delete <filename>\n";
                continue;
            }

            if (!metadataManager.fileExists(filename)) {
                cout << "File not found: " << filename << endl;
                continue;
            }

            journalManager.log_operation("delete", filename);
            if (metadataManager.deleteFile(filename)) {
                cout << "File deleted: " << filename << endl;
            } else {
                cout << "Error deleting file: " << filename << endl;
            }
            continue;
        }

        if (command == "list") {
            vector<string> files = metadataManager.listFiles();
            if (files.empty()) {
                cout << "No files.\n";
            } else {
                cout << "Files:\n";
                for (const auto& file : files) {
                    cout << " - " << file << endl;
                }
            }
            continue;
        }

        if (command == "snapshot") {
            string subcommand;
            if (!(iss >> subcommand)) {
                cout << "Usage: snapshot <create|rollback|list> [...]\n";
                continue;
            }

            if (subcommand == "create") {
                string name;
                if (!(iss >> name)) {
                    cout << "Usage: snapshot create <name>\n";
                    continue;
                }
                int snapshot_id = snapshotManager.create_snapshot(name, metadataManager);
                cout << "Snapshot created: " << name << " (ID " << snapshot_id << ")\n";
            } else if (subcommand == "rollback") {
                string id_token;
                if (!(iss >> id_token)) {
                    cout << "Usage: snapshot rollback <snapshot_id>\n";
                    continue;
                }
                int snapshot_id = 0;
                if (!parse_snapshot_id(id_token, snapshot_id)) {
                    cout << "Snapshot ID must be an integer.\n";
                    continue;
                }
                if (snapshotManager.rollback(snapshot_id, metadataManager, journalManager)) {
                    cout << "Rolled back to snapshot ID: " << snapshot_id << endl;
                } else {
                    cout << "Snapshot not found for ID: " << snapshot_id << endl;
                }
            } else if (subcommand == "list") {
                vector<Snapshot> snapshots = snapshotManager.list_snapshots();
                if (snapshots.empty()) {
                    cout << "No snapshots.\n";
                } else {
                    cout << "Snapshots:\n";
                    for (const auto& snapshot : snapshots) {
                        cout << " - ID " << snapshot.id << " : " << snapshot.name << endl;
                    }
                }
            } else {
                cout << "Unknown snapshot command: " << subcommand << endl;
            }
            continue;
        }

        cout << "Unknown command: " << command << ". Type 'help' for usage.\n";
    }

    return 0;
}
