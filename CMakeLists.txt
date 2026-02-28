cmake_minimum_required(VERSION 3.20)
project(DeltaFS VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Protobuf REQUIRED)
find_package(gRPC REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)
find_library(UUID_LIBRARY uuid REQUIRED)

set(PROTO_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/proto)
set(GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GENERATED_DIR})

set(PROTO_FILES
  proto/common.proto
  proto/blockstore.proto
  proto/replication.proto
  proto/journal.proto
  proto/metadata.proto
  proto/snapshot.proto
  proto/admin.proto
)

set(PROTO_SRCS)
set(PROTO_HDRS)

foreach(PROTO_FILE ${PROTO_FILES})
  get_filename_component(PROTO_ABS ${PROTO_FILE} ABSOLUTE)
  get_filename_component(PROTO_NAME ${PROTO_FILE} NAME_WE)

  set(PB_SRC ${GENERATED_DIR}/${PROTO_NAME}.pb.cc)
  set(PB_HDR ${GENERATED_DIR}/${PROTO_NAME}.pb.h)
  set(GRPC_SRC ${GENERATED_DIR}/${PROTO_NAME}.grpc.pb.cc)
  set(GRPC_HDR ${GENERATED_DIR}/${PROTO_NAME}.grpc.pb.h)

  add_custom_command(
    OUTPUT ${PB_SRC} ${PB_HDR} ${GRPC_SRC} ${GRPC_HDR}
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
      --proto_path=${PROTO_SRC_DIR}
      --cpp_out=${GENERATED_DIR}
      --grpc_out=${GENERATED_DIR}
      --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
      ${PROTO_ABS}
    DEPENDS ${PROTO_ABS}
    VERBATIM
  )

  list(APPEND PROTO_SRCS ${PB_SRC} ${GRPC_SRC})
  list(APPEND PROTO_HDRS ${PB_HDR} ${GRPC_HDR})
endforeach()

add_library(deltafs_proto ${PROTO_SRCS} ${PROTO_HDRS})
target_include_directories(deltafs_proto PUBLIC ${GENERATED_DIR})
target_link_libraries(deltafs_proto
  PUBLIC
    gRPC::grpc++
    protobuf::libprotobuf
)

add_library(deltafs_common
  src/common/status.cpp
  src/common/logging.cpp
  src/common/utils.cpp
  src/common/config.cpp
  src/common/grpc_utils.cpp
  src/common/dedupe_store.cpp
)

target_include_directories(deltafs_common
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${GENERATED_DIR}
)

target_link_libraries(deltafs_common
  PUBLIC
    deltafs_proto
    ${UUID_LIBRARY}
)

add_library(deltafs_core
  src/blockstore/block_store.cpp
  src/journal/journal_manager.cpp
  src/metadata/metadata_manager.cpp
  src/snapshot/snapshot_manager.cpp
  src/membership/membership_provider.cpp
  src/replication/replication_manager.cpp
  src/replication/replica_node.cpp
  src/services/service_helpers.cpp
  src/services/node_services.cpp
  src/services/request_context_interceptor.cpp
)

target_include_directories(deltafs_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${GENERATED_DIR}
)

target_link_libraries(deltafs_core
  PUBLIC
    deltafs_common
    ZLIB::ZLIB
    Threads::Threads
)

add_executable(deltafs_node src/node_main.cpp)
target_link_libraries(deltafs_node PRIVATE deltafs_core)

add_executable(deltafs_ctl src/ctl_main.cpp)
target_link_libraries(deltafs_ctl PRIVATE deltafs_common)

enable_testing()

add_executable(deltafs_test_wal tests/unit/test_wal.cpp)
target_link_libraries(deltafs_test_wal PRIVATE deltafs_core)
add_test(NAME deltafs_test_wal COMMAND deltafs_test_wal)

add_executable(deltafs_test_snapshot tests/unit/test_snapshot.cpp)
target_link_libraries(deltafs_test_snapshot PRIVATE deltafs_core)
add_test(NAME deltafs_test_snapshot COMMAND deltafs_test_snapshot)

add_executable(deltafs_test_idempotency tests/unit/test_idempotency.cpp)
target_link_libraries(deltafs_test_idempotency PRIVATE deltafs_core)
add_test(NAME deltafs_test_idempotency COMMAND deltafs_test_idempotency)
