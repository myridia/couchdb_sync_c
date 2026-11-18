# How to Use `couchdb_sync_c`

This guide provides detailed, step-by-step instructions for building, integrating, and using `couchdb_sync_c` to synchronize documents between a CouchDB server and a local SQLite database.

> Important: this is an MVP implementation. It supports separated `pull`, `push`, and `sync` APIs with resumable checkpoints, but it does not yet implement full PouchDB/CouchDB replication fidelity (full rev-tree/merge/conflict handling, complete attachment replication, live sync, etc.). See [Limitations](#11-limitations-and-notes).

---

## 1. Prerequisites

### 1.1 System requirements

- A Linux/Unix-like environment (examples assume a shell)
- A C11-compatible compiler (`gcc` or `clang`)
- CMake **3.16+**
- Development libraries:
  - `libcurl` (HTTP client)
  - `sqlite3` (local storage)
- A reachable CouchDB server (for actual replication)

### 1.2 Install build dependencies

Install packages using your system package manager (run on your host/machine):

**Debian/Ubuntu**
```sh
sudo apt-get update
sudo apt-get install -y build-essential cmake pkg-config libcurl4-openssl-dev libsqlite3-dev
```

**Fedora**
```sh
sudo dnf install -y gcc gcc-c++ make cmake pkgconf-pkg-config libcurl-devel sqlite-devel
```

**Arch Linux**
```sh
sudo pacman -S --needed base-devel cmake curl sqlite
```

> Package names may vary slightly by distribution/version.

### 1.3 CouchDB prerequisites

You need:
- CouchDB server URL (e.g. `http://127.0.0.1:5984`)
- A database name (e.g. `mydb`)
- Credentials if your CouchDB requires authentication

Example checks (optional, run on your host):

```sh
# Check server is reachable
curl http://127.0.0.1:5984/

# Create a database (adjust URL/auth as needed)
curl -X PUT http://127.0.0.1:5984/mydb

# Add a sample document
curl -X POST http://127.0.0.1:5984/mydb \
  -H 'Content-Type: application/json' \
  -d '{"_id":"doc1","type":"example","hello":"world"}'
```

If authentication is enabled:

```sh
curl -u USER:PASS http://127.0.0.1:5984/mydb
```

---

## 2. Get the project

Project location in this workspace:

```text
/home/veto/webs/github/couchdb_sync_c
```

Key paths:
- Public header: `include/couchdb_sync.h`
- Example: `examples/pull_once.c`
- Task runner: `ask.sh` and `ask.d/`
- API docs: `docs/api.md`

Move into the project directory:

```sh
cd /home/veto/webs/github/couchdb_sync_c
```

---

## 3. Build the library

You can build with CMake directly or use the provided `ask.sh` tasks.

### 3.1 Option A: Use `ask.sh` (recommended)

From the project root:

```sh
./ask.sh
# or
sh ask.sh
```

Available tasks:
- `1` **BUILD** — Configure + build: `cmake -S . -B build -DBUILD_TESTING=ON && cmake --build build`
- `2` **BUILD** — Compile only: `cmake --build build` (requires existing configured `build/`)
- `3` **TEST** — Run tests: `ctest --test-dir build --output-on-failure`
- `4` **BUILD** — Configure + build shared: `cmake -S . -B build -DBUILD_TESTING=ON -DBUILD_SHARED_LIBS=ON && cmake --build build`
- `0` — Exit

Typical first build:
1. Run `./ask.sh`
2. Enter `1`
3. Wait for configure/build to complete

### 3.2 Option B: Use CMake directly

**Configure + build (static library by default):**
```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
```

**Compile only (after initial configure):**
```sh
cmake --build build
```

**Build as shared library:**
```sh
cmake -S . -B build -DBUILD_TESTING=ON -DBUILD_SHARED_LIBS=ON
cmake --build build
```

### 3.3 Expected build outputs

After a successful build, look in `build/`:

- `build/libcouchdb_sync_c.a` — static library (default)
- `build/libcouchdb_sync_c.so` — shared library (if built with `BUILD_SHARED_LIBS=ON`, on Linux)
- `build/couchdb_sync_example` — example executable
- `build/tests/test_api` — basic test binary

> Exact filenames/extensions depend on platform and configuration.

---

## 4. Run the basic test (optional)

After building with tests enabled:

```sh
ctest --test-dir build --output-on-failure
```

Or use `ask.sh` task `3`.

This runs a basic API sanity test (`tests/test_api.c`). It does **not** require a running CouchDB server.

---

## 5. Understand the public API

Include the public header in your C code:

```c
#include "couchdb_sync.h"
```

### 5.1 Configuration (`couchdb_sync_config`)

```c
couchdb_sync_config config = {0};
config.server_url = "http://127.0.0.1:5984"; /* required */
config.db_name    = "mydb";                   /* required */
config.sqlite_path = "local.sqlite3";         /* required */
config.username   = "user";                   /* optional, NULL if unused */
config.password   = "pass";                   /* optional, NULL if unused */
```

- `server_url`, `db_name`, and `sqlite_path` must be non-`NULL`.
- Strings are copied by `couchdb_sync_open()`.

### 5.2 Open and close

```c
couchdb_sync *sync = NULL;
if (couchdb_sync_open(&config, &sync) != 0) {
    /* handle error */
}
/* ... use sync ... */
couchdb_sync_close(sync);
```

- `couchdb_sync_open()` creates/opens the SQLite file, initializes schema, and prepares HTTP/checkpoint state.
- `couchdb_sync_close()` releases all resources (safe with `NULL`).

### 5.3 Replication operations

```c
int couchdb_sync_pull(couchdb_sync *sync, couchdb_sync_result *result);
int couchdb_sync_push(couchdb_sync *sync, couchdb_sync_result *result);
int couchdb_sync_sync(couchdb_sync *sync, couchdb_sync_bidi_result *result);
```

- **Pull:** remote CouchDB → local SQLite
- **Push:** local SQLite → remote CouchDB
- **Sync:** bidirectional (runs push, then pull) with separate checkpoints

Return value: `0` on success, non-zero on failure. `result` may be `NULL`.

### 5.4 Results

```c
couchdb_sync_result res = {0};
/* res.docs_read, res.docs_written, res.last_seq */
```

- `docs_read`: change entries/docs read from source
- `docs_written`: docs written/applied to target
- `last_seq`: heap-allocated string (final checkpoint sequence) when provided

If you pass a non-`NULL` result and replication succeeds, free `last_seq` when done:

```c
free((void *)res.last_seq);
```

For bidirectional sync:

```c
couchdb_sync_bidi_result bres = {0};
couchdb_sync_sync(sync, &bres);
free((void *)bres.push.last_seq);
free((void *)bres.pull.last_seq);
```

See [`docs/api.md`](api.md) for full API details.

---

## 6. Step-by-step: Pull replication (remote → local)

Use pull to download/sync documents from CouchDB into a local SQLite database.

### Step 1: Create a source file

Create `pull_example.c` (or use `examples/pull_once.c` as a base):

```c
#include <stdio.h>
#include <stdlib.h>
#include "couchdb_sync.h"

int main(void) {
    couchdb_sync_config config = {0};
    couchdb_sync *sync = NULL;
    couchdb_sync_result result = {0};

    config.server_url = "http://127.0.0.1:5984";
    config.db_name = "mydb";
    config.sqlite_path = "local.sqlite3";

    if (couchdb_sync_open(&config, &sync) != 0) {
        fprintf(stderr, "open failed\n");
        return 1;
    }

    if (couchdb_sync_pull(sync, &result) != 0) {
        fprintf(stderr, "pull failed\n");
        couchdb_sync_close(sync);
        return 1;
    }

    printf("pull: docs_read=%ld docs_written=%ld last_seq=%s\n",
           result.docs_read, result.docs_written,
           result.last_seq ? result.last_seq : "(null)");

    free((void *)result.last_seq);
    couchdb_sync_close(sync);
    return 0;
}
```

### Step 2: Compile your program against the library

From the project root (after building the library):

```sh
cc -Iinclude pull_example.c -Lbuild -lcouchdb_sync_c -lcurl -lsqlite3 -o pull_example
```

Notes:
- Adjust compiler, paths, and library order if needed on your system.
- If you built a shared library, ensure the runtime linker can find it (e.g. `LD_LIBRARY_PATH=build ./pull_example` or install the library appropriately).

### Step 3: Run the pull

```sh
./pull_example
```

Expected behavior:
- Connects to CouchDB and reads changes since the last pull checkpoint
- Stores received documents into `local.sqlite3`
- Prints counts and the final checkpoint sequence

### Step 4: Run again to confirm incremental sync

Run the same program again:

```sh
./pull_example
```

On a second run with no remote changes, `docs_read`/`docs_written` should typically be `0` (or minimal) because the pull resumes from the stored checkpoint.

### Step 5: Inspect the local SQLite database (optional)

```sh
sqlite3 local.sqlite3 ".tables"
sqlite3 local.sqlite3 "SELECT id, seq, deleted, json FROM 'by-sequence' LIMIT 10;"
sqlite3 local.sqlite3 "SELECT id, rev, json FROM 'local-store';"
```

You should see tables such as `document-store`, `by-sequence`, and `local-store` (which stores replication checkpoints).

---

## 7. Step-by-step: Push replication (local → remote)

Use push to upload local changes from SQLite to CouchDB.

### Step 1: Understand what push sends

Current MVP push reads local change rows from the `by-sequence` table (ordered by `seq`) since the push checkpoint, checks missing revisions via CouchDB `_revs_diff`, and uploads missing docs via `_bulk_docs` with `new_edits:false`.

This means push is driven by locally stored sequence records. In a typical workflow:
- pull remote data first (populates local store), then
- make/store local changes in a way that creates new `by-sequence` entries (advanced usage), then
- push those changes back.

> The library does not yet expose a high-level "create/update/delete document" API. Local write workflows currently depend on how documents/revisions are stored in SQLite.

### Step 2: Create a push program

```c
#include <stdio.h>
#include <stdlib.h>
#include "couchdb_sync.h"

int main(void) {
    couchdb_sync_config config = {0};
    couchdb_sync *sync = NULL;
    couchdb_sync_result result = {0};

    config.server_url = "http://127.0.0.1:5984";
    config.db_name = "mydb";
    config.sqlite_path = "local.sqlite3";

    if (couchdb_sync_open(&config, &sync) != 0) {
        fprintf(stderr, "open failed\n");
        return 1;
    }

    if (couchdb_sync_push(sync, &result) != 0) {
        fprintf(stderr, "push failed\n");
        couchdb_sync_close(sync);
        return 1;
    }

    printf("push: docs_read=%ld docs_written=%ld last_seq=%s\n",
           result.docs_read, result.docs_written,
           result.last_seq ? result.last_seq : "(null)");

    free((void *)result.last_seq);
    couchdb_sync_close(sync);
    return 0;
}
```

### Step 3: Compile and run

```sh
cc -Iinclude push_example.c -Lbuild -lcouchdb_sync_c -lcurl -lsqlite3 -o push_example
./push_example
```

If there are no local changes since the push checkpoint, `docs_read` and `docs_written` will be `0`.

---

## 8. Step-by-step: Bidirectional sync (push + pull)

Use `couchdb_sync_sync()` to synchronize in both directions. The current implementation runs **push first, then pull**, using separate checkpoints/replication ids for each direction.

### Step 1: Create a sync program

```c
#include <stdio.h>
#include <stdlib.h>
#include "couchdb_sync.h"

int main(void) {
    couchdb_sync_config config = {0};
    couchdb_sync *sync = NULL;
    couchdb_sync_bidi_result result = {0};

    config.server_url = "http://127.0.0.1:5984";
    config.db_name = "mydb";
    config.sqlite_path = "local.sqlite3";

    if (couchdb_sync_open(&config, &sync) != 0) {
        fprintf(stderr, "open failed\n");
        return 1;
    }

    if (couchdb_sync_sync(sync, &result) != 0) {
        fprintf(stderr, "sync failed\n");
        free((void *)result.push.last_seq);
        free((void *)result.pull.last_seq);
        couchdb_sync_close(sync);
        return 1;
    }

    printf("push: read=%ld written=%ld last_seq=%s\n",
           result.push.docs_read, result.push.docs_written,
           result.push.last_seq ? result.push.last_seq : "(null)");
    printf("pull: read=%ld written=%ld last_seq=%s\n",
           result.pull.docs_read, result.pull.docs_written,
           result.pull.last_seq ? result.pull.last_seq : "(null)");

    free((void *)result.push.last_seq);
    free((void *)result.pull.last_seq);
    couchdb_sync_close(sync);
    return 0;
}
```

### Step 2: Compile and run

```sh
cc -Iinclude sync_example.c -Lbuild -lcouchdb_sync_c -lcurl -lsqlite3 -o sync_example
./sync_example
```

### Step 3: Repeat as needed

Run the same program periodically/on demand to incrementally synchronize. Each direction resumes from its own checkpoint.

---

## 9. Using authentication and different environments

### 9.1 HTTP basic authentication

Set `username` and `password` in the config:

```c
config.server_url = "http://127.0.0.1:5984";
config.db_name = "mydb";
config.sqlite_path = "local.sqlite3";
config.username = "admin";
config.password = "secret";
```

### 9.2 HTTPS

Use an `https://` server URL if your CouchDB is served over TLS:

```c
config.server_url = "https://couchdb.example.com";
```

TLS behavior depends on your libcurl/OpenSSL build and system trust store.

### 9.3 Multiple local databases

You can use different `sqlite_path` values for different local replicas/caches. Checkpoints are stored inside each SQLite database, so each local file tracks its own replication progress.

---

## 10. Integrating `couchdb_sync_c` into your own project

### 10.1 Include and link

At minimum, your build needs:
- include path: `.../couchdb_sync_c/include`
- library path: `.../couchdb_sync_c/build` (or installed location)
- link libraries: `couchdb_sync_c`, `curl`, `sqlite3`

Example (manual compile/link):

```sh
cc -I/path/to/couchdb_sync_c/include \
   myapp.c \
   -L/path/to/couchdb_sync_c/build -lcouchdb_sync_c -lcurl -lsqlite3 \
   -o myapp
```

### 10.2 CMake-based consumer project (example)

```cmake
cmake_minimum_required(VERSION 3.16)
project(myapp C)
set(CMAKE_C_STANDARD 11)

add_executable(myapp myapp.c)

target_include_directories(myapp PRIVATE /path/to/couchdb_sync_c/include)
target_link_directories(myapp PRIVATE /path/to/couchdb_sync_c/build)

find_package(CURL REQUIRED)
find_package(SQLite3 REQUIRED)

target_link_libraries(myapp PRIVATE couchdb_sync_c CURL::libcurl SQLite::SQLite3)
```

> Adjust paths/integration style for your environment (subdirectory, installed package, etc.).

### 10.3 Recommended integration pattern

1. Prepare `couchdb_sync_config` with server/db/local path (+ auth if needed).
2. Call `couchdb_sync_open()`.
3. Run `pull`, `push`, or `sync` as required by your app flow.
4. Check return values (`0` success, non-zero failure).
5. If you requested results, read counters/`last_seq` and `free()` any returned `last_seq` strings.
6. Call `couchdb_sync_close()`.

---

## 11. Limitations and notes

Be aware of current MVP constraints:

- **Error detail:** API returns success/failure only (no rich error codes/messages).
- **JSON handling:** minimal internal parsing/building for current paths; a full JSON library integration is planned.
- **Pull path:** uses `/_changes` with `include_docs=true`; does not yet implement full `revsDiff`/`bulkGet`/`open_revs` flow.
- **Push path:** uses local `by-sequence` changes, `/_revs_diff`, and `/_bulk_docs` (`new_edits:false`) for MVP; behavior depends on locally stored revision data.
- **Attachments:** schema exists, but full attachment replication/handling is incomplete.
- **Conflicts/revisions:** full rev-tree merge, winning-rev/conflict resolution, and compaction are not yet implemented.
- **Sync mode:** `couchdb_sync_sync()` performs push then pull (non-live). No continuous/live replication yet.
- **Thread safety:** not guaranteed; synchronize access externally if sharing a handle across threads.
- **Result ownership:** `last_seq` strings returned in results are heap-allocated and must be freed by the caller when present.

For design/background, see:
- [`docs/api.md`](api.md)
- [`docs/pouchdb-mapping.md`](pouchdb-mapping.md)
- [`../PLAN.md`](../PLAN.md)

---

## 12. Troubleshooting

### Build/configure issues

- **CMake cannot find libcurl/sqlite3:** install the development packages (`libcurl*-dev`/`libcurl-devel`, `libsqlite3-dev`/`sqlite-devel`) and re-run configure (task `1` or `4`).
- **Compile-only task fails:** run configure+build first (task `1` or `4`) so `build/` exists and is configured.

### Runtime/replication failures

- **`couchdb_sync_open` fails:** verify `server_url`, `db_name`, and `sqlite_path` are all non-`NULL`; check filesystem permissions for the SQLite path.
- **`pull`/`push`/`sync` fails:**
  - confirm CouchDB is reachable from your environment
  - verify database exists and credentials (if required) are correct
  - check network/TLS issues for HTTPS endpoints
- **No documents appear after pull:** confirm the remote database actually contains documents and the correct `db_name`/server were used.
- **Second run shows no new changes:** expected when already up to date; checkpoints resume from the last successful sequence.

### Inspecting local state

```sh
sqlite3 local.sqlite3 ".tables"
sqlite3 local.sqlite3 "SELECT id, rev, json FROM 'local-store' WHERE id LIKE '_local/%';"
sqlite3 local.sqlite3 "SELECT seq, doc_id, rev, deleted FROM 'by-sequence' ORDER BY seq DESC LIMIT 20;"
```

This can help confirm checkpoint progress and stored revisions.

---

## 13. Quick reference

### Commands

```sh
# Build (task runner)
./ask.sh   # 1: configure+build, 2: compile, 3: test, 4: shared build

# Build (direct)
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

### Minimal pull program

```c
couchdb_sync_config cfg = {0};
couchdb_sync *sync = NULL;
couchdb_sync_result res = {0};
cfg.server_url = "http://127.0.0.1:5984";
cfg.db_name = "mydb";
cfg.sqlite_path = "local.sqlite3";
if (couchdb_sync_open(&cfg, &sync) == 0 && couchdb_sync_pull(sync, &res) == 0) {
    /* success */
}
free((void *)res.last_seq);
couchdb_sync_close(sync);
```
