cmake_minimum_required(VERSION 3.10)
project(DeltaFS VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_BUILD_TYPE Debug)

include_directories(src)

add_executable(deltafs
    src/main.cpp
    src/block_store.cpp
    src/metadata_manager.cpp
    src/snapshot_manager.cpp
    src/journal_manager.cpp
    src/replica_node.cpp
)

# Add this line to link libuuid
target_link_libraries(deltafs PRIVATE uuid)