#include "block_store.h"
#include <uuid/uuid.h>

std::string BlockStore::write_block(const std::string& data) {
    std::lock_guard<std::mutex> lock(mtx);
    uuid_t uuid;
    char id_str[37];
    uuid_generate(uuid);
    uuid_unparse(uuid, id_str);
    blocks[id_str] = data;
    return std::string(id_str);
}

std::string BlockStore::read_block(const std::string& id) {
    std::lock_guard<std::mutex> lock(mtx);
    return blocks.count(id) ? blocks[id] : "";
}