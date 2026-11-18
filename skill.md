# Skill: Using `couchdb_sync_c`

This document is a practical guide for AI agents working with `couchdb_sync_c`, a C library that synchronizes documents between a CouchDB server and a local SQLite database (PouchDB/CouchDB replication concepts, MVP implementation).

## 1. Project location and key files

- **Project root:** `/home/veto/webs/github/couchdb_sync_c`
- **Public API header:** `include/couchdb_sync.h`
- **Implementation:** `src/` (`couchdb_sync.c`, `sync_pull.c`, `sync_push.c`, `store_sqlite.c`, `http_curl.c`, `checkpointer.c`, `replication_id.c`, etc.)
- **Examples:** `examples/pull_once.c`, `examples/simple_sync.c`
- **Tests:** `tests/test_api.c` (CTest)
- **Build system:** `CMakeLists.txt`
- **Task runner:** `ask.sh`, `ask.d/`
- **Docs:** `README.md`, `docs/api.md`, `docs/howtouse.md`, `docs/tutorial.md`, `docs/pouchdb-mapping.md`, `PLAN.md`

## 2. What the library does (current MVP)

- Provides separated replication APIs (PouchDB-style direction operations):
  - **Pull:** remote CouchDB → local SQLite
  - **Push:** local SQLite → remote CouchDB
  - **Sync:** bidirectional (`push` then `pull`)
- Stores data in a local SQLite file (schema aligned with PouchDB websql-core store names: `metadata-store`, `document-store`, `by-sequence`, `local-store`, `attach-store`, `attach-seq-store`)
- Persists checkpoints in `local-store` as `_local/<replication-id>` for resumable incremental sync
- Uses `libcurl` for HTTP and `sqlite3` for storage

### Important limitations (do not assume full PouchDB parity)
- No rich error reporting API (functions return `0` success / non-zero failure)
- Minimal internal JSON parsing/building (not a full JSON library)
- Pull uses `GET /_changes?...&style=all_docs&include_docs=true` (MVP path), not full `revsDiff`/`bulkGet`/`open_revs`
- Push uses local `by-sequence` changes + `POST /_revs_diff` + `POST /_bulk_docs` (`new_edits:false`) (MVP path)
- Attachments, full rev-tree merge/winning/conflict resolution, live/continuous sync, retry/backoff, filters/selectors/views, and remote checkpoint coordination are incomplete or not implemented
- Not thread-safe without external synchronization
- No public high-level document CRUD API; local writes/revision management are not exposed as a full app-facing storage API

## 3. Public API (from `include/couchdb_sync.h`)

```c
typedef struct couchdb_sync couchdb_sync;

typedef struct {
    const char *server_url;   /* required */
    const char *db_name;      /* required */
    const char *sqlite_path;  /* required */
    const char *username;     /* optional */
    const char *password;     /* optional */
} couchdb_sync_config;

typedef struct {
    long docs_read;
    long docs_written;
    const char *last_seq;     /* heap-allocated when returned; caller frees */
} couchdb_sync_result;

typedef struct {
    couchdb_sync_result push;
    couchdb_sync_result pull;
} couchdb_sync_bidi_result;

int  couchdb_sync_open(const couchdb_sync_config *config, couchdb_sync **out);
int  couchdb_sync_pull(couchdb_sync *sync, couchdb_sync_result *result);
int  couchdb_sync_push(couchdb_sync *sync, couchdb_sync_result *result);
int  couchdb_sync_sync(couchdb_sync *sync, couchdb_sync_bidi_result *result);
void couchdb_sync_close(couchdb_sync *sync);
```

### API usage rules
- `server_url`, `db_name`, and `sqlite_path` must be non-`NULL` for `couchdb_sync_open()`.
- Config strings are copied by `couchdb_sync_open()`.
- `result` may be `NULL` for pull/push/sync calls.
- On success with a non-`NULL` result, `last_seq` (and in bidi sync, `push.last_seq` / `pull.last_seq`) may be heap-allocated by the library; **caller must free** with `free((void *)last_seq)` when present.
- If `result` is `NULL`, the library frees internal `last_seq` strings.
- `couchdb_sync_close(NULL)` is safe.
- `couchdb_sync_sync()` runs **push then pull** and returns non-zero if either direction fails.

## 4. Minimal usage patterns

### 4.1 Pull (remote → local)

```c
#include <stdio.h>
#include <stdlib.h>
#include "couchdb_sync.h"

int main(void) {
    couchdb_sync_config cfg = {0};
    couchdb_sync *sync = NULL;
    couchdb_sync_result res = {0};

    cfg.server_url = "http://127.0.0.1:5984";
    cfg.db_name = "mydb";
    cfg.sqlite_path = "local.sqlite3";
    /* cfg.username = "user"; cfg.password = "pass"; */

    if (couchdb_sync_open(&cfg, &sync) != 0) return 1;
    if (couchdb_sync_pull(sync, &res) != 0) {
        free((void *)res.last_seq);
        couchdb_sync_close(sync);
        return 1;
    }

    printf("pull: read=%ld written=%ld last_seq=%s\n",
           res.docs_read, res.docs_written, res.last_seq ? res.last_seq : "(null)");

    free((void *)res.last_seq);
    couchdb_sync_close(sync);
    return 0;
}
```

### 4.2 Push (local → remote)

```c
couchdb_sync_result res = {0};
if (couchdb_sync_push(sync, &res) != 0) { /* handle */ }
free((void *)res.last_seq);
```

### 4.3 Bidirectional sync

```c
couchdb_sync_bidi_result res = {0};
if (couchdb_sync_sync(sync, &res) != 0) { /* handle */ }
/* use res.push.* and res.pull.* */
free((void *)res.push.last_seq);
free((void *)res.pull.last_seq);
```

## 5. Building the library and examples

> Build/compile/test commands must be run by the user (agent should not execute builds/tests).

### 5.1 Direct CMake

From `/home/veto/webs/github/couchdb_sync_c`:

```sh
# Configure + build (static by default)
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build

# Compile only (after configure)
cmake --build build

# Run tests
ctest --test-dir build --output-on-failure

# Configure + build shared library
cmake -S . -B build -DBUILD_TESTING=ON -DBUILD_SHARED_LIBS=ON
cmake --build build
```

### 5.2 Task runner (`ask.sh`)

```sh
./ask.sh
```

Tasks (`ask.d/`):
- `1` BUILD — Configure + build
- `2` BUILD — Compile only
- `3` TEST — Run ctest
- `4` BUILD — Configure + build (shared)

### 5.3 Expected outputs

- `build/libcouchdb_sync_c.a` (static, default) or `build/libcouchdb_sync_c.so` (shared, Linux)
- `build/couchdb_sync_example` (`examples/pull_once.c`)
- `build/simple_sync` (`examples/simple_sync.c`)
- `build/tests/test_api`

## 6. Compiling and linking a consumer app

A consumer program needs:
- include path: `include/`
- link to: `couchdb_sync_c`, `curl`, `sqlite3`

### 6.1 Manual compile/link example

```sh
cc -Iinclude myapp.c -Lbuild -lcouchdb_sync_c -lcurl -lsqlite3 -o myapp
```

If using a shared library and runtime lookup fails, run with library path set (example):

```sh
LD_LIBRARY_PATH=build ./myapp
```

### 6.2 CMake consumer example

```cmake
add_executable(myapp myapp.c)
target_include_directories(myapp PRIVATE /path/to/couchdb_sync_c/include)
target_link_directories(myapp PRIVATE /path/to/couchdb_sync_c/build)
find_package(CURL REQUIRED)
find_package(SQLite3 REQUIRED)
target_link_libraries(myapp PRIVATE couchdb_sync_c CURL::libcurl SQLite::SQLite3)
```

## 7. Runtime behavior and data model (agent-relevant)

- **Pull checkpoint:** stored in local SQLite `local-store` as `_local/<pull-replication-id>`; pull resumes from remote changes `since` sequence.
- **Push checkpoint:** stored separately as `_local/<push-replication-id>`; push resumes from local `by-sequence` integer seq (stored as string).
- **Sync:** push and pull use different replication ids/checkpoints.
- **Local schema tables:** `metadata-store`, `document-store`, `by-sequence`, `local-store`, `attach-store`, `attach-seq-store`.
- **Useful inspection queries (user-run):**
  - `SELECT id, rev, json FROM 'local-store' WHERE id LIKE '_local/%';`
  - `SELECT seq, doc_id, rev, deleted FROM 'by-sequence' ORDER BY seq;`

## 8. Common modification points for agents

- **Public API changes:** `include/couchdb_sync.h` and corresponding implementations in `src/couchdb_sync.c`, `src/sync_pull.c`, `src/sync_push.c`, `src/internal.h`
- **Pull protocol behavior:** `src/sync_pull.c` (currently changes feed + `include_docs=true`)
- **Push protocol behavior:** `src/sync_push.c` (local changes, `_revs_diff`, `_bulk_docs`)
- **HTTP requests/auth/transport:** `src/http_curl.c`
- **SQLite schema/storage/checkpoints/change feed:** `src/store_sqlite.c`, `src/checkpointer.c`
- **Replication id generation:** `src/replication_id.c`
- **Build targets/tasks/tests:** `CMakeLists.txt`, `ask.sh`, `ask.d/*.sh`, `tests/`
- **Documentation:** `README.md`, `docs/api.md`, `docs/howtouse.md`, `docs/tutorial.md`, `docs/pouchdb-mapping.md`, `PLAN.md`

## 9. Agent constraints and best practices for this workspace/project

- Follow workspace rules in `~/webs/agents/AGENTS.md` when working in this environment (no git commands by agent, no build/test/compile/lint/format execution by agent, no comments in code unless asked, keep responses concise).
- Preserve existing project style and literal naming.
- When adding/changing files in the project, apply workspace file ownership/permissions conventions after edits (as required by the workspace).
- After API or behavior changes, update `docs/api.md`, `README.md`, examples, and this `skill.md` as needed to keep them consistent.
- Be explicit in reports about what was changed vs. what remains unbuilt/untested.

## 10. Quick reference

- **Header:** `#include "couchdb_sync.h"`
- **Open:** `couchdb_sync_open(&cfg, &sync)`
- **Pull:** `couchdb_sync_pull(sync, &res)`
- **Push:** `couchdb_sync_push(sync, &res)`
- **Sync:** `couchdb_sync_sync(sync, &bidi_res)` (push then pull)
- **Close:** `couchdb_sync_close(sync)`
- **Example app:** `examples/simple_sync.c` (`build/simple_sync` after build)
- **Primary docs:** `docs/api.md`, `docs/howtouse.md`, `docs/tutorial.md`
