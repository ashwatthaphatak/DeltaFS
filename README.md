# DeltaFS Distributed Playground (C++ gRPC)

DeltaFS is a WAFL-inspired distributed storage playground implemented in **C++ only** with **protobuf + gRPC**.

Milestone 1 is intentionally minimal and correctness-first:
- one codebase
- one node binary (`deltafs_node`)
- one CLI (`deltafs_ctl`)
- one gRPC API surface
- deployment-specific behavior isolated to membership discovery adapters

## WAFL-Inspired Design (What It Means Here)
- Immutable blocks: writes allocate new block files (no in-place overwrite).
- Root pointer model: metadata is versioned by root version.
- Simplified consistency point: leader advances committed root only after
  1. local WAL append + fsync
  2. replication ack policy satisfied (`one`, `majority`, `all`)
  3. in-order apply
- Read-only snapshots: snapshot stores root pointer at creation time.

## What Is Simplified vs Production
- Leader selection is static by config (node0 / deltafs-0 leader by default).
- Replication protocol is primary-backup style with append/catch-up; not full Raft.
- No compaction/GC/rebalancing yet.
- Metadata namespace is KV-first (paths/inodes can be layered later).

## Components
- Block Store: immutable block persistence under `/var/lib/deltafs/blocks`.
- Journal Manager (WAL): segmented binary WAL with CRC32 + fsync.
- Replication Manager/Node: leader append + follower catch-up + ack policy.
- Metadata Manager: root-versioned KV map (copy-on-write root snapshots).
- Snapshot Manager: snapshot ID -> root version.
- Dedupe Store: persisted `request_id` table under `/var/lib/deltafs/dedupe`.

Persistent layout on each node:
- `/var/lib/deltafs/blocks`
- `/var/lib/deltafs/wal`
- `/var/lib/deltafs/metadata`
- `/var/lib/deltafs/snapshots`
- `/var/lib/deltafs/dedupe`

## gRPC API Surfaces
- `BlockStoreService`: `PutBlock`, `GetBlock`
- `JournalService`: `AppendEntry`, `GetJournalStatus`
- `MetadataService`: `PutKey`, `GetKey`
- `SnapshotService`: `CreateSnapshot`, `ListSnapshots`, `ReadAtSnapshot`
- `ReplicationService`: `AppendEntries`
- `AdminService`: `GetStatus`, `ForceConsistencyPoint`, `PromoteToLeader`

Every request carries request context (`request_id`, `node_id`, optional client timestamp).

## Build and Test
```bash
make build
make test
```

Binaries:
- `build/deltafs_node`
- `build/deltafs_ctl`

## Local 3-Node Cluster (Docker Compose)
```bash
make compose-up
```

Run demo scenario (writes, snapshot validation, follower kill/restart, catch-up):
```bash
./scripts/demo_compose.sh
```

Tear down:
```bash
make compose-down
```

## Kubernetes (Minikube First)
Build image into cluster runtime (done by demo script), deploy StatefulSet + headless Service:
```bash
./scripts/demo_k8s.sh
```

Manual deploy:
```bash
make k8s-up
```

Manual teardown:
```bash
make k8s-down
```

`kind` overlay is included at `deploy/k8s/overlays/kind`.

## CLI Usage
```bash
# write
build/deltafs_ctl --endpoint 127.0.0.1:50051 --node-id cli put key value

# read
build/deltafs_ctl --endpoint 127.0.0.1:50051 --node-id cli get key

# create snapshot
build/deltafs_ctl --endpoint 127.0.0.1:50051 --node-id cli snapshot create S1

# read at snapshot
build/deltafs_ctl --endpoint 127.0.0.1:50051 --node-id cli snapshot get snap-... key

# status
build/deltafs_ctl --endpoint 127.0.0.1:50051 --node-id cli status
```

## Recovery Semantics
On restart, node loads on-disk modules and replays committed WAL state via persisted replication indices:
- WAL is replayed from segments with CRC validation.
- Replication state restores `commit_index` and `last_applied`.
- Metadata committed root is restored from disk.
- Dedupe table persists prior `request_id` outcomes.

## Kubernetes gRPC Caveat (Important)
Kubernetes Service balancing is connection-level, while gRPC uses long-lived HTTP/2 connections. That can pin traffic to a single pod.

Milestone 1 therefore uses:
- StatefulSet for stable pod identity
- **headless Service** + stable pod DNS endpoints
- direct peer addressing/client endpoint pools

No service mesh/proxy is required yet, but the architecture keeps hooks for adding one later.

## Future Hooks
- Raft for dynamic leader election and stronger log semantics
- gossip/SDK-backed membership providers
- WAL compaction and metadata snapshotting
- block checksumming and background scrubbing
- garbage collection and data rebalancing
