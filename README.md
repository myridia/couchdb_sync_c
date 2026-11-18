<img src="couchdb_sync_c.svg" alt="couchdb_sync_c logo" width="120">

# couchdb_sync_c

`couchdb_sync_c` is a C library for synchronizing documents between a CouchDB server and a local SQLite database. It is a reimplementation of PouchDB/CouchDB replication and storage concepts in C (not a port of PouchDB's JavaScript code), using the local PouchDB source in `../pouchdb` as behavioral reference.

The library currently provides separated replication APIs in the style of PouchDB direction-specific operations:
- **Pull:** remote CouchDB → local SQLite
- **Push:** local SQLite → remote CouchDB
- **Sync:** bidirectional (push, then pull) with independent checkpoints

> Status: MVP implementation scaffold. It is suitable for experimentation and incremental development, but not yet a complete, fully compatible PouchDB/CouchDB replication engine.

## Contents

- [Features](#features)
- [API Overview](#api-overview)
- [Build and Test](#build-and-test)
- [Quick Start](#quick-start)
- [Usage Examples](#usage-examples)
- [How Replication Works (Current MVP)](#how-replication-works-current-mvp)
- [Local SQLite Storage](#local-sqlite-storage)
- [Checkpoints and Replication IDs](#checkpoints-and-replication-ids)
- [Project Layout](#project-layout)
- [Documentation](#documentation)
- [Limitations and Roadmap](#limitations-and-roadmap)
- [PouchDB Reference](#pouchdb-reference)

## Features

- C11 library with an opaque handle API (`couchdb_sync_*`)
- CouchDB over HTTP via `libcurl` (optional HTTP basic auth)
- Local persistence in SQLite3
- Separated APIs:
  - `couchdb_sync_pull()`
  - `couchdb_sync_push()`
  - `couchdb_sync_sync()` (bidirectional)
- Resumable replication via stored checkpoints (`_local/<replication-id>`) in local SQLite
- Direction-specific replication ids/checkpoints for pull and push
- SQLite schema and store naming aligned with PouchDB websql-core concepts (`metadata-store`, `document-store`, `by-sequence`, `local-store`, `attach-store`, `attach-seq-store`)
- CMake build, CTest-based basic tests, and `ask.sh`/`ask.d` task runner

## API Overview

Public header: [`include/couchdb_sync.h`](include/couchdb_sync.h)  
Detailed reference: [`docs/api.md`](docs/api.md)

### Core types

- `couchdb_sync` — opaque sync context handle
- `couchdb_sync_config` — server/database/sqlite path and optional credentials
- `couchdb_sync_result` — result of a pull or push run (`docs_read`, `docs_written`, `last_seq`)
- `couchdb_sync_bidi_result` — combined `{ push, pull }` result for bidirectional sync

### Core functions

```c
int  couchdb_sync_open(const couchdb_sync_config *config, couchdb_sync **out);
int  couchdb_sync_pull(couchdb_sync *sync, couchdb_sync_result *result);
int  couchdb_sync_push(couchdb_sync *sync, couchdb_sync_result *result);
int  couchdb_sync_sync(couchdb_sync *sync, couchdb_sync_bidi_result *result);
void couchdb_sync_close(couchdb_sync *sync);
```

Return convention: `0` on success, non-zero on failure.

## Build and Test

### Requirements

- CMake (>= 3.16)
- C11 compiler
- `libcurl` development package
- `sqlite3` development package

### Commands

From `/home/veto/webs/github/couchdb_sync_c`:

```sh
# Configure + build (default)
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build

# Run tests
ctest --test-dir build --output-on-failure

# Shared library build
cmake -S . -B build -DBUILD_TESTING=ON -DBUILD_SHARED_LIBS=ON
cmake --build build
```

### Task runner (`ask.sh` / `ask.d`)

```sh
./ask.sh
# or
sh ask.sh
```

Tasks:
- `1` BUILD — Configure + build (`cmake -S . -B build -DBUILD_TESTING=ON && cmake --build build`)
- `2` BUILD — Compile only (`cmake --build build`)
- `3` TEST — Run tests (`ctest --test-dir build --output-on-failure`)
- `4` BUILD — Configure + build shared (`-DBUILD_SHARED_LIBS=ON`)

### Build outputs (expected)

After a successful build in `build/`:
- Library: `build/libcouchdb_sync_c.a` (static, default) or `build/libcouchdb_sync_c.so` (shared)
- Example: `build/couchdb_sync_example`
- Test binary: `build/tests/test_api`

(Exact names/paths can vary by platform and CMake configuration.)

## Quick Start

1. Ensure CouchDB is reachable and the target database exists.
2. Build the library and example.
3. Run a pull into a local SQLite file.

Minimal example (`examples/pull_once.c`):

```c
#include <stdio.h>
#include "couchdb_sync.h"

int main(void) {
    couchdb_sync_config config = {0};
    couchdb_sync *sync = NULL;
    couchdb_sync_result result = {0};

    config.server_url = "http://127.0.0.1:5984";
    config.db_name = "example";
    config.sqlite_path = "example.sqlite3";

    if (couchdb_sync_open(&config, &sync) != 0) {
        fprintf(stderr, "open failed\n");
        return 1;
    }

    if (couchdb_sync_pull(sync, &result) != 0) {
        fprintf(stderr, "pull failed\n");
        couchdb_sync_close(sync);
        return 1;
    }

    printf("docs_read=%ld docs_written=%ld\n", result.docs_read, result.docs_written);
    couchdb_sync_close(sync);
    return 0;
}
```

## Usage Examples

### Pull (remote CouchDB → local SQLite)

```c
couchdb_sync_config config = {0};
couchdb_sync *sync = NULL;
couchdb_sync_result res = {0};

config.server_url = "http://127.0.0.1:5984";
config.db_name = "mydb";
config.sqlite_path = "local.sqlite3";
config.username = "user";      /* optional */
config.password = "pass";      /* optional */

if (couchdb_sync_open(&config, &sync) == 0) {
    if (couchdb_sync_pull(sync, &res) == 0) {
        /* res.docs_read, res.docs_written, res.last_seq */
    }
    free((void *)res.last_seq); /* if set */
    couchdb_sync_close(sync);
}
```

### Push (local SQLite → remote CouchDB)

```c
couchdb_sync_result res = {0};
if (couchdb_sync_push(sync, &res) == 0) {
    /* res.docs_read, res.docs_written, res.last_seq */
}
free((void *)res.last_seq);
```

### Bidirectional sync

```c
couchdb_sync_bidi_result res = {0};
if (couchdb_sync_sync(sync, &res) == 0) {
    /* res.push.*, res.pull.* */
}
free((void *)res.push.last_seq);
free((void *)res.pull.last_seq);
```

Notes:
- `result` parameters are optional (`NULL` allowed).
- When provided, `last_seq` is heap-allocated by the library; caller should `free((void *)last_seq)` when present.
- `couchdb_sync_sync()` runs **push then pull** (separate checkpoints/replication ids), similar in structure to PouchDB `sync`.

## How Replication Works (Current MVP)

The implementation follows PouchDB/CouchDB replication concepts but is intentionally simplified.

### Pull (remote → local)

1. Load pull checkpoint (`last_seq`) from local `_local/<pull-replication-id>`; default `"0"`.
2. Fetch remote changes in batches:  
   `GET /{db}/_changes?feed=normal&style=all_docs&since=<seq>&limit=100&include_docs=true`
3. For each change item, extract `id`, leaf `rev`, `deleted`, and included `doc` JSON.
4. Store document revisions in local SQLite (`by-sequence` + `document-store` update) inside a transaction per batch.
5. Write updated pull checkpoint and continue until caught up.

### Push (local → remote)

1. Load push checkpoint (local sequence, string form) from local `_local/<push-replication-id>`; default `"0"`.
2. Read local changes from `by-sequence` where `seq > since` (batched, ascending).
3. Ask remote which revs are missing: `POST /{db}/_revs_diff`.
4. Upload missing revisions: `POST /{db}/_bulk_docs` with `new_edits:false`.
5. Write updated push checkpoint and continue until no local changes remain.

### Bidirectional sync

- Executes `push`, then `pull`.
- Uses separate, direction-specific replication ids and checkpoints (PouchDB-like separation of push/pull replications).

## Local SQLite Storage

On open, the library initializes a schema modeled after PouchDB websql-core stores:

- `metadata-store` — database metadata
- `document-store` — document metadata/winning sequence tracking
- `by-sequence` — per-revision rows (`seq`, `doc_id`, `rev`, `deleted`, `json`) and indexes
- `local-store` — non-replicated local docs, including checkpoints (`_local/<replication-id>`)
- `attach-store` — attachment storage (schema present; full replication handling is MVP/incomplete)
- `attach-seq-store` — attachment/sequence linkage (schema present)

The storage layer is implemented in `src/store_sqlite.c`. Current behavior is an MVP mapping and does not yet implement full revision-tree merge/conflict semantics.

## Checkpoints and Replication IDs

- Checkpoints are stored locally in `local-store` as `_local/<replication-id>` documents with fields such as `last_seq`, `session_id`, `version`, and `replicator`.
- Pull and push use **different replication ids** (direction-sensitive), so their progress is tracked independently.
- Replication ids are currently deterministic MVP hashes derived from endpoint/path/direction (not byte-for-byte identical to PouchDB's md5-based ids, but serving the same role).

## Project Layout

```text
include/                Public API header(s)
src/                    Library implementation
  couchdb_sync.c        Open/close + public API entry points
  http_curl.c           HTTP client (libcurl)
  store_sqlite.c        SQLite schema and storage operations
  checkpointer.c        Checkpoint get/write helpers
  replication_id.c      Direction-sensitive replication id generation
  sync_pull.c           Pull replication (remote -> local)
  sync_push.c           Push replication (local -> remote)
  rev.c                 Revision parsing utilities
  util.c                Internal helpers
  internal.h            Internal types and declarations
examples/               Usage examples
tests/                  CTest-based basic tests
ask.sh, ask.d/          Task runner (configure/build/compile/test/shared)
docs/                   Design and API documentation
PLAN.md                 PouchDB-to-C/SQLite mapping and next steps
```

## Documentation

- [API Reference](docs/api.md) — public types, functions, behavior, ownership, and examples
- [PouchDB Mapping Notes](docs/pouchdb-mapping.md) — inspected PouchDB modules and current C mapping/status
- [PLAN.md](PLAN.md) — implementation plan, schema mapping, and next steps

## Limitations and Roadmap

### Current limitations (MVP)

- No detailed public error reporting (functions return success/failure only)
- Minimal/purpose-built JSON parsing and generation (not a full JSON library integration)
- Pull path uses `changes` with `include_docs=true`; does not yet implement full `revsDiff`/`bulkGet` (or `open_revs`) document fetch flow
- Push uploads stored revision JSON from local changes; full local edit/rev-tree management APIs are not exposed
- Attachments: schema exists, but end-to-end attachment replication/handling is incomplete
- No full PouchDB-style revision-tree merge, winning rev/conflict resolution, or compaction logic
- No live/continuous replication, retry/backoff policies, selectors/filters/views replication options, or remote checkpoint coordination
- Not thread-safe without external synchronization

### Planned/next steps

- Integrate a robust JSON library and typed document models
- Implement pull-side `revsDiff`/`bulkGet`/attachment handling (closer to `getDocs.js`)
- Port/adapt `pouchdb-merge` and `pouchdb-adapter-utils` logic (`parseDoc`, `merge`, `winningRev`, `updateDoc`) for correctness
- Add richer error reporting and configuration options
- Add live/continuous sync, retry/backoff, filtering/selectors, and compaction
- Expand tests (integration tests against CouchDB, conflict/attachment cases)

## PouchDB Reference

Local clone used as reference: `../pouchdb`

Primary modules consulted:
- `pouchdb-replication/src/replicate.js`, `getDocs.js`, `sync.js`, `replicateWrapper.js`, `replication.js`
- `pouchdb-checkpointer/src/index.js`
- `pouchdb-generate-replication-id/src/index.js`
- `pouchdb-adapter-http/src/index.js`
- `pouchdb-adapter-websql-core/src/{index,constants,utils,bulkDocs}.js`
- `pouchdb-adapter-utils/src/{parseDoc,processDocs,updateDoc}.js`
- `pouchdb-merge/src/{merge,winningRev,collectLeaves,...}.js`
