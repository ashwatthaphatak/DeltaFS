#include "block_store.h"
#include <uuid/uuid.h>

using namespace std;

string BlockStore::write_block(const string& data) {
    lock_guard<mutex> lock(mtx);
    uuid_t uuid;
    char id_str[37];
    uuid_generate(uuid);
    uuid_unparse(uuid, id_str);
    blocks[id_str] = data;
    return string(id_str);
}

string BlockStore::read_block(const string& id) {
    lock_guard<mutex> lock(mtx);
    return blocks.count(id) ? blocks[id] : "";
}
