# Plan: PouchDB logic to C + SQLite

## Local reference

PouchDB source is cloned at `../pouchdb` and used as the implementation reference for this reimplementation.

Key modules inspected:
- `pouchdb-replication/src/replicate.js`: batch change processing, pending batches, checkpoint-after-write flow, `revsDiff`/`bulkDocs({new_edits:false})` path, retry/completion.
- `pouchdb-checkpointer/src/index.js`: `_local/<replication-id>` checkpoint doc with `last_seq`, `session_id`, history (`CHECKPOINT_VERSION`, `REPLICATOR`, `CHECKPOINT_HISTORY_SIZE`), source/target/session comparison.
- `pouchdb-generate-replication-id/src/index.js`: deterministic id from source/target ids and replication options.
- `pouchdb-adapter-websql-core/src/{constants,index,utils,bulkDocs}.js`: SQLite/WebSQL schema and behavior (`metadata-store`, `document-store`, `by-sequence`, `attach-store`, `local-store`, `attach-seq-store`), indexes, doc/rev storage, local docs, compaction helpers.
- `pouchdb-adapter-utils/src/{parseDoc,processDocs,updateDoc}.js` and `pouchdb-merge/src/*`: doc parsing, rev trees, merge/stem, winning rev, conflict handling.

## C + SQLite mapping

### Components

- `src/http_curl.c` (libcurl): CouchDB HTTP JSON requests, auth, timeout.
- `src/util.c`: small helpers (string/json field extraction for MVP, uuid/session generation).
- `src/store_sqlite.c`: SQLite schema and operations modeled on websql-core stores/indexes.
- `src/checkpointer.c`: checkpoint read/write in local-store (`_local/<replication-id>`).
- `src/replication_id.c`: deterministic replication id (MVP hash over source/db/sqlite path/options).
- `src/sync_pull.c`: checkpointed pull loop following `replicate.js` flow.
- `src/rev.c`: rev parsing utilities (foundation for merge/rev-tree work).

### SQLite schema (aligned with websql-core)

- `metadata-store(dbid PRIMARY KEY, db_version, max_seq)`
- `document-store(id UNIQUE, json, winningseq, max_seq)` + `doc-winningseq-idx`
- `by-sequence(seq INTEGER PRIMARY KEY AUTOINCREMENT, json, deleted, doc_id, rev)` + `by-seq-deleted-idx`, `by-seq-doc-id-rev`
- `local-store(id PRIMARY KEY, rev, json)` for checkpoints and other non-replicated local docs
- `attach-store(digest PRIMARY KEY, json, body)`
- `attach-seq-store(seq, digest)` + indexes

### Sync behavior mapping

| PouchDB/CouchDB | C implementation |
|---|---|
| `replicate(from,to)` / `sync` (`push` + `pull`) | `couchdb_sync_pull`, `couchdb_sync_push`, `couchdb_sync_sync` (push then pull) with separate direction-specific replication ids/checkpoints |
| `generateReplicationId` | `couchdb_replication_id(..., push)` deterministic MVP id (direction-sensitive) |
| `Checkpointer.getCheckpoint/writeCheckpoint` | `couchdb_checkpointer_get/write` via `local-store` `_local/<id>` |
| Pull: `src.changes` batches (`style=all_docs`) | `GET /{db}/_changes?feed=normal&style=all_docs&since=...&limit=...&include_docs=true` (MVP) |
| Push: local changes -> `target.revsDiff` -> `bulkDocs({new_edits:false})` | local `by-sequence` changes since checkpoint -> `POST /_revs_diff` -> `POST /_bulk_docs` with `new_edits:false` (MVP) |
| Pull apply | `store_put_doc` inserts `by-sequence` rev row and updates `document-store` in a checkpointed transaction |
| checkpoint after batch commit | apply/write + checkpoint persisted (pull in DB transaction per batch; push advances checkpoint after successful upload) |

### Next implementation steps

1. Replace minimal JSON extraction/building with a proper JSON library and typed models.
2. Implement pull-side `revsDiff`/`bulkGet` (or `open_revs`/`all_docs`) and attachment handling like `getDocs.js`.
3. Port `pouchdb-merge`/`pouchdb-adapter-utils` rev-tree logic (`parseDoc`, `merge`, `winningRev`, `updateDoc`) for full `new_edits:false` correctness and conflicts.
4. Add live/continuous sync, retry/backoff, filtered/selector replication options, remote checkpoint writes, and compaction.
