#include <filesystem>
#include <iostream>

#include "deltafs/journal/journal_manager.h"
#include "deltafs/common/utils.h"

int main() {
  const std::string base = "/tmp/deltafs_test_wal_" + deltafs::common::GenerateRequestId();
  std::filesystem::create_directories(base);

  {
    deltafs::journal::JournalManager journal(base + "/wal", 1024 * 1024);
    auto status = journal.Initialize();
    if (!status.ok()) {
      std::cerr << "initialize failed: " << status.message << "\n";
      return 1;
    }

    deltafs::v1::WalEntry entry;
    entry.set_request_id("req-1");
    entry.set_origin_node_id("test");
    entry.set_operation(deltafs::v1::WAL_OPERATION_PUT_KEY);
    entry.set_key("k1");
    entry.set_block_id("b1");
    entry.set_value("v1");
    entry.set_base_root_version(0);
    entry.set_new_root_version(1);

    deltafs::v1::WalEntry appended;
    status = journal.AppendNewEntry(entry, &appended);
    if (!status.ok()) {
      std::cerr << "append failed: " << status.message << "\n";
      return 1;
    }

    if (appended.lsn() != 1) {
      std::cerr << "expected lsn=1 got " << appended.lsn() << "\n";
      return 1;
    }
  }

  {
    deltafs::journal::JournalManager replayed(base + "/wal", 1024 * 1024);
    auto status = replayed.Initialize();
    if (!status.ok()) {
      std::cerr << "replay initialize failed: " << status.message << "\n";
      return 1;
    }

    if (replayed.last_lsn() != 1) {
      std::cerr << "expected last_lsn=1 after replay got " << replayed.last_lsn() << "\n";
      return 1;
    }

    deltafs::v1::WalEntry got;
    status = replayed.GetEntry(1, &got);
    if (!status.ok()) {
      std::cerr << "GetEntry failed: " << status.message << "\n";
      return 1;
    }

    if (got.key() != "k1" || got.value() != "v1") {
      std::cerr << "unexpected replayed payload\n";
      return 1;
    }
  }

  std::filesystem::remove_all(base);
  std::cout << "ok\n";
  return 0;
}
