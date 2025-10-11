This is an attemt at making a good versioned Distributed FS.

Currently planned HLD looks something like this -

```
+--------------------------------------------------------------+
| DeltaFS CLI |
| (main.cpp) – User command parsing and dispatching |
+-------------------------+------------------------------------+
|
v
+-------------------------+------------------------------------+
| Core Managers |
|--------------------------------------------------------------|
| BlockStore | Manages raw block writes and reads |
| MetadataManager | Maintains file ↔ block metadata |
| SnapshotManager | Handles snapshot creation & rollback |
| JournalManager | Persists logs for recovery |
| ReplicaNode | Syncs metadata across network nodes |
+--------------------------------------------------------------+
```

```
             ┌──────────────────────────────┐
             │          CLI (main)          │
             │ parse & dispatch user cmds   │
             └──────────────┬───────────────┘
                            │
            ┌───────────────┼────────────────┐
            │               │                │
┌───────────▼────────┐ ┌────▼────────────┐ ┌─▼──────────────┐
│   BlockStore       │ │ MetadataManager │ │ SnapshotManager │
│ store/read blocks  │ │ map files→blocks│ │ copy/rollback   │
└────────┬───────────┘ └────────┬────────┘ └────────┬────────┘
         │                      │                   │
         ▼                      ▼                   ▼
    ┌────────────┐        ┌─────────────┐       ┌───────────────┐
    │JournalMgr  │        │ReplicaNode  │       │ Persistent Data│
    │log ops     │        │sync meta    │       │ (disk, json)   │
    └────────────┘        └─────────────┘       └───────────────┘
```
