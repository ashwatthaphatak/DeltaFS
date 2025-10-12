#pragma once
#include <cstddef>

// Default block size for splitting file payloads across the block store.
constexpr std::size_t kDefaultBlockSize = 4096;
