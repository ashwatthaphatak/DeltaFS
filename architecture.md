# DeltaFS Architecture (WAFL-Inspired, Distributed Playground)

This document explains DeltaFS from the architecture level down to the concrete C++ files that implement each part. It is intentionally aligned to the current Milestone 1 code: **primary-backup replication**, **static leader**, **KV metadata**, and **root-pointer snapshots**.

## Purpose And Design Principles

- **One codebase, one node binary, one API surface**
  - Server binary: `src/node_main.cpp` (builds `deltafs_node`)
  - CLI binary: `src/ctl_main.cpp` (builds `deltafs_ctl`)
  - Protobuf/gRPC API: `proto/*.proto` (codegen via `CMakeLists.txt`)
- **WAFL-inspired model (simplified)**
  - Immutable blocks: new writes allocate new block files; no in-place overwrite.
  - Root pointer model: metadata is versioned by monotonically increasing `root_version`.
  - Consistency point (simplified): leader advances `commit_index` only after WAL append+fsync and replication ack policy is satisfied.
- **Correctness and debuggability over features**
  - Write idempotency via persisted request dedupe.
  - Structured error model in `proto/common.proto`.
  - Request context injection into logs via a gRPC interceptor.

## High-Level Component Map

### Node Entrypoints

- `src/node_main.cpp`
  - Loads config (`deltafs::common::LoadNodeConfig`).
  - Constructs `deltafs::replication::ReplicaNode`.
  - Registers gRPC services and starts a gRPC server.

- `src/ctl_main.cpp`
  - Implements `deltafs_ctl` commands: `put`, `get`, `snapshot create|get|list`, `status`.
  - Sets request context (`request_id`, `node_id`) and deadlines, adds metadata headers.

### Orchestrator: `ReplicaNode`

- Interface: `include/deltafs/replication/replica_node.h`
- Implementation: `src/replication/replica_node.cpp`

`ReplicaNode` is the composition root for all storage/replication modules:
- Block store (`BlockStore`)
- WAL (`JournalManager`)
- Metadata roots (`MetadataManager`)
- Snapshots (`SnapshotManager`)
- Request idempotency (`DedupeStore`)
- Membership discovery adapter (`MembershipProvider`)
- Replication/commit orchestration (`ReplicationManager`)

All user-visible behavior ultimately routes through `ReplicaNode` methods called by the gRPC services.

### Service Layer (Thin RPC Adapters)

- Service classes: `include/deltafs/services/node_services.h`
- Implementations: `src/services/node_services.cpp`
- Request validation helpers: `src/services/service_helpers.cpp`
- Request context/logging interceptor: `src/services/request_context_interceptor.cpp`

The service layer should remain thin:
- Validate `RequestContext` exists.
- Call into `ReplicaNode`.
- Return application status in the response’s `ErrorStatus` (not as a non-OK gRPC status).

## Protocol / API Layer (protobuf + gRPC)

All request/response types live under `proto/`:

- Request context and status model: `proto/common.proto`
  - `RequestContext`: `request_id`, `node_id`, optional client timestamp.
  - `ErrorStatus`: `code`, `message`, `retryable`, plus key/value details.

- Block store service: `proto/blockstore.proto`
  - `PutBlock` / `GetBlock` for immutable local blocks.

- Journal service: `proto/journal.proto`
  - `AppendEntry` (leader path convenience) and `GetJournalStatus`.

- Metadata service: `proto/metadata.proto`
  - `PutKey` / `GetKey` for KV namespace.

- Snapshot service: `proto/snapshot.proto`
  - `CreateSnapshot`, `ListSnapshots`, `ReadAtSnapshot`.

- Replication service: `proto/replication.proto`
  - `WalEntry` (replicated log entry).
  - `AppendEntries` (leader -> follower replication RPC).

- Admin/status service: `proto/admin.proto`
  - `GetStatus`, `ForceConsistencyPoint`, `PromoteToLeader`.

Generated sources are emitted into the build directory (`${CMAKE_BINARY_DIR}/generated`) by `CMakeLists.txt` and linked into the node/ctl targets.

## Storage And State Subsystems (On-Disk)

The node data dir is configured via `NodeConfig.data_dir` (defaults to `/var/lib/deltafs` in deploy configs). Subdirectories:

- `blocks/` immutable block payload files
- `wal/` WAL segment files
- `metadata/` root maps and replication state
- `snapshots/` snapshot metadata
- `dedupe/` request_id dedupe log

### Block Store (Immutable Blocks)

- Interface: `include/deltafs/blockstore/block_store.h`
- Implementation: `src/blockstore/block_store.cpp`

What happens:
- `PutBlock(data)` writes a new file under `blocks/` (atomic temp + rename) and fsyncs the file.
- `GetBlock(block_id)` reads the file by name.
- `PutBlockWithId(block_id, data)` is used by WAL apply on followers to ensure the block exists under the exact replicated `block_id`.

### Journal / WAL (Segmented, CRC, fsync)

- Interface: `include/deltafs/journal/journal_manager.h`
- Implementation: `src/journal/journal_manager.cpp`

What happens:
- WAL is a sequence of `WalEntry` messages stored as records:
  - header: payload size + crc32
  - payload: `WalEntry` serialized bytes
- On append:
  - record is written to the active segment
  - `fsync()` is called before returning success
- On startup:
  - segment files are read in lexical order
  - CRC is validated and LSN sequence is enforced
  - the next segment is opened for appends

### Metadata Roots (Root Pointer Model)

- Interface: `include/deltafs/metadata/metadata_manager.h`
- Implementation: `src/metadata/metadata_manager.cpp`

What happens:
- Metadata is stored as a map `key -> block_id` per root version.
- Each root version is persisted as `metadata/roots/root_<N>.meta`.
- The current committed root pointer is persisted as `metadata/committed_root`.
- `CommitRoot(root_version)` advances the committed pointer (used during WAL apply).

Important note for Milestone 1:
- Root versions are advanced by WAL entries; the system treats the committed root as “the filesystem view”.

### Snapshots (Root Pointer At Time Of Creation)

- Interface: `include/deltafs/snapshot/snapshot_manager.h`
- Implementation: `src/snapshot/snapshot_manager.cpp`

What happens:
- Snapshot records are stored in `snapshots/snapshots.meta`.
- A snapshot is a mapping `snapshot_id -> root_version`.
- Reads at snapshot:
  - resolve `snapshot_id` to a `root_version`
  - do metadata lookup at that root
  - read the corresponding block

### Request Dedupe / Idempotency

- Interface: `include/deltafs/common/dedupe_store.h`
- Implementation: `src/common/dedupe_store.cpp`

What happens:
- Each write-like request uses `RequestContext.request_id`.
- Before applying a write, `ReplicaNode` checks the dedupe store.
- After a write is committed/applied, the result is persisted to `dedupe/requests.log`.
- On restart, the dedupe table is loaded and idempotency survives process restarts.

## Replication And Consistency

### Replication Manager

- Interface: `include/deltafs/replication/replication_manager.h`
- Implementation: `src/replication/replication_manager.cpp`

Key state:
- `commit_index`: last committed WAL LSN
- `last_applied`: last WAL LSN applied to local storage modules
- `leader_id`: current leader identity (static by config in milestone 1)

What happens (leader):
- Append `WalEntry` locally (WAL fsync).
- Replicate `AppendEntries` to followers until ack policy is satisfied.
- Advance `commit_index`, then apply entries in order (advances committed root).

What happens (follower):
- Accept `AppendEntries` and append replicated entries contiguously.
- When `leader_commit` advances, apply committed entries in order.

Persistence for recovery:
- replication state file: `metadata/replication_state` (written by `ReplicationManager`).

### Where Commit/Application Happens

The “apply” step is implemented in `ReplicaNode::ApplyWalEntry`:
- `src/replication/replica_node.cpp`
  - `WAL_OPERATION_PUT_KEY`: ensure block exists (`PutBlockWithId`), update metadata root (`ApplyReplicatedPut`), then `CommitRoot(new_root_version)`.
  - `WAL_OPERATION_CREATE_SNAPSHOT`: create snapshot record and commit root (root pointer for snapshot).

This is the core of the “write-anywhere feel” in Milestone 1: changes are applied by allocating new blocks and creating new root versions rather than overwriting old state.

## Membership / Discovery Abstraction

- Interface + implementations: `include/deltafs/membership/membership_provider.h`, `src/membership/membership_provider.cpp`
- Config parsing: `include/deltafs/common/config.h`, `src/common/config.cpp`

What happens:
- Compose mode (`membership_provider: compose`): peers come from the config `peers:` list.
- K8s mode (`membership_provider: k8s`): peers are derived from StatefulSet + headless service DNS:
  - `<statefulset>-<ordinal>.<headless>.<namespace>.svc.cluster.local:<port>`

Business logic does not hardcode hostnames; it asks the membership provider for peer endpoints.

## Common Infrastructure

- Config: `include/deltafs/common/config.h`, `src/common/config.cpp`
  - Parses a simple YAML-like config format used by deploy templates.
  - Provides ack policy parsing and required ack count.

- Status model: `include/deltafs/common/status.h`, `src/common/status.cpp`
  - `StatusCode` + `Status` mapping to/from `proto/common.proto::ErrorStatus`.

- gRPC context helpers: `include/deltafs/common/grpc_utils.h`, `src/common/grpc_utils.cpp`
  - Populates `RequestContext` and adds it as gRPC metadata headers.
  - Maps transport gRPC errors to internal status.

- Logging + request scoping: `include/deltafs/common/logging.h`, `src/common/logging.cpp`
  - Thread-local context for `request_id` and `node_id`.
  - gRPC interceptor sets/clears this context automatically:
    - `src/services/request_context_interceptor.cpp`

- Misc utils: `include/deltafs/common/utils.h`, `src/common/utils.cpp`
  - UUID request ID generation, filesystem helpers, time helpers.

## Runtime Sequence Walkthroughs (File-Level)

### 1) `put key value` end-to-end

1. Client constructs RPC and context:
   - `src/ctl_main.cpp` (`deltafs_ctl put ...`)
2. gRPC service handler validates context and forwards to `ReplicaNode`:
   - `src/services/node_services.cpp` (`MetadataServiceImpl::PutKey`)
3. `ReplicaNode` idempotency and request shaping:
   - `src/replication/replica_node.cpp` (`ReplicaNode::PutKey`)
   - checks `DedupeStore` (`src/common/dedupe_store.cpp`)
   - writes a new immutable block (`src/blockstore/block_store.cpp`)
   - builds a `WalEntry` and calls replication manager
4. Leader WAL append + replication + commit/apply:
   - `src/replication/replication_manager.cpp` (`ReplicateClientEntry`)
   - `src/journal/journal_manager.cpp` (append + fsync)
   - follower RPC path: `proto/replication.proto` + `src/services/node_services.cpp` (`ReplicationServiceImpl::AppendEntries`)
5. Apply committed WAL entry to durable state:
   - `src/replication/replica_node.cpp` (`ApplyWalEntry`)
   - updates metadata root (`src/metadata/metadata_manager.cpp`) and commits new root pointer
6. Record request result for idempotency:
   - `src/common/dedupe_store.cpp`

### 2) `snapshot create` and `snapshot get`

- Create snapshot:
  1. Client: `src/ctl_main.cpp` (`snapshot create`)
  2. Service: `src/services/node_services.cpp` (`SnapshotServiceImpl::CreateSnapshot`)
  3. `ReplicaNode::CreateSnapshot` builds a WAL entry:
     - `src/replication/replica_node.cpp`
  4. Replication + commit:
     - `src/replication/replication_manager.cpp`
  5. Apply snapshot record on commit:
     - `src/replication/replica_node.cpp` (`ApplyWalEntry`)
     - `src/snapshot/snapshot_manager.cpp` persists snapshot mapping

- Read at snapshot:
  1. Client: `src/ctl_main.cpp` (`snapshot get`)
  2. Service: `src/services/node_services.cpp` (`SnapshotServiceImpl::ReadAtSnapshot`)
  3. `ReplicaNode::ReadAtSnapshot` resolves snapshot root:
     - `src/replication/replica_node.cpp` calls `SnapshotManager::GetSnapshotRoot`
     - `src/snapshot/snapshot_manager.cpp`
  4. Metadata lookup at that root and block read:
     - `src/metadata/metadata_manager.cpp`
     - `src/blockstore/block_store.cpp`

### 3) Follower down + recovery/catch-up (milestone 1 behavior)

Relevant files:
- replication/catch-up loop: `src/replication/replication_manager.cpp` (`ReplicateToPeerLocked`)
- follower append/apply: `src/replication/replication_manager.cpp` (`HandleAppendEntries`) + `src/replication/replica_node.cpp` (`ApplyWalEntry`)

What happens:
- Leader continues to append locally, and attempts to replicate to available followers to satisfy the ack policy.
- When the follower restarts, leader sends missing WAL entries starting at follower’s last known LSN and the follower applies up to leader commit.

### 4) Node restart recovery

Startup ordering:
1. `src/node_main.cpp` constructs `ReplicaNode`.
2. `ReplicaNode::Initialize` initializes:
   - block store (`src/blockstore/block_store.cpp`)
   - WAL replay (`src/journal/journal_manager.cpp`)
   - metadata root loading (`src/metadata/metadata_manager.cpp`)
   - snapshot loading (`src/snapshot/snapshot_manager.cpp`)
   - dedupe loading (`src/common/dedupe_store.cpp`)
3. `ReplicationManager::Initialize` loads `metadata/replication_state` and applies committed entries:
   - `src/replication/replication_manager.cpp`
   - applies through callback into `ReplicaNode::ApplyWalEntry`

## Build And Generation Pipeline

All build and codegen wiring is in `CMakeLists.txt`:
- Generates `*.pb.cc/*.pb.h` and `*.grpc.pb.cc/*.grpc.pb.h` into `${CMAKE_BINARY_DIR}/generated`.
- Libraries:
  - `deltafs_proto`: generated proto/grpc sources
  - `deltafs_common`: config/status/logging/utils/dedupe
  - `deltafs_core`: storage + replication + services
- Binaries:
  - `deltafs_node`: server
  - `deltafs_ctl`: CLI

## Deployment Mapping

### Docker Compose

- Compose file: `deploy/compose/docker-compose.yml`
- Node configs: `deploy/configs/node0.yaml`, `deploy/configs/node1.yaml`, `deploy/configs/node2.yaml`
- Demo script: `scripts/demo_compose.sh`

### Kubernetes (StatefulSet + Headless Service)

- Base manifests: `deploy/k8s/base/service-headless.yaml`, `deploy/k8s/base/statefulset.yaml`
- Overlays: `deploy/k8s/overlays/minikube`, `deploy/k8s/overlays/kind`
- Demo script: `scripts/demo_k8s.sh`

Kubernetes note (why headless + direct endpoints):
- Kubernetes Service load balancing is connection-level; gRPC uses long-lived HTTP/2 connections, which can pin traffic to one pod. For Milestone 1, stable pod DNS identities are used directly.

## Testing And Verification Pointers

- Unit tests (C++ binaries run under CTest):
  - WAL append/replay: `tests/unit/test_wal.cpp`
  - Snapshot root semantics: `tests/unit/test_snapshot.cpp`
  - Request idempotency: `tests/unit/test_idempotency.cpp`

- Integration scripts:
  - Compose: `tests/integration/compose/test_compose.sh` and `scripts/demo_compose.sh`
  - K8s: `tests/integration/k8s/test_k8s.sh` and `scripts/demo_k8s.sh`

## Suggested Reading Order (Fast Onboarding)

1. `proto/common.proto` and `proto/replication.proto` (request context + WAL entry shape)
2. `src/node_main.cpp` and `src/services/node_services.cpp` (server wiring)
3. `src/replication/replica_node.cpp` (core orchestration + apply path)
4. `src/replication/replication_manager.cpp` (commit/replication logic)
5. `src/journal/journal_manager.cpp` (durability + replay)
6. `src/metadata/metadata_manager.cpp` and `src/snapshot/snapshot_manager.cpp` (root pointers + snapshot reads)
7. `src/blockstore/block_store.cpp` (immutable block persistence)
8. `deploy/` (compose + k8s wiring)

