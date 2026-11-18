# Tutorial: Build a Simple App Using `couchdb_sync_c`

This tutorial walks you through building a simple command-line application that uses `couchdb_sync_c` to synchronize documents between a CouchDB server and a local SQLite database.

By the end, you will have an app (`simple_sync`) that can run:
- `pull` — remote CouchDB → local SQLite
- `push` — local SQLite → remote CouchDB
- `sync` — bidirectional (push, then pull)

You will also learn how to compile, run, and inspect the results.

> Note: `couchdb_sync_c` is currently an MVP. This tutorial reflects the current API and behavior.

---

## What you will build

A small CLI program with this interface:

```text
simple_sync <pull|push|sync> <server_url> <db_name> <sqlite_path> [username] [password]
```

Examples:

```sh
./build/simple_sync pull http://127.0.0.1:5984 mydb ./local.sqlite3
./build/simple_sync push http://127.0.0.1:5984 mydb ./local.sqlite3 admin secret
./build/simple_sync sync http://127.0.0.1:5984 mydb ./local.sqlite3
```

The source for this example is included in the repository as `examples/simple_sync.c`.

---

## Prerequisites

Before starting, make sure you have:

1. **Build tools and libraries** (on your host/machine)
   - C compiler (`cc`, `gcc`, or `clang`)
   - CMake 3.16+
   - `libcurl` development headers/library
   - `sqlite3` development headers/library

2. **A CouchDB server** you can reach
   - Server URL (e.g. `http://127.0.0.1:5984`)
   - A database to sync (e.g. `mydb`)
   - Username/password if authentication is required

3. **Project source**
   - This library project at: `/home/veto/webs/github/couchdb_sync_c`

> Install dependencies and run CouchDB/build commands on your host environment. This tutorial shows the commands to run; it does not execute them for you.

---

## Step 1: Open the project directory

Open a terminal and go to the library project root:

```sh
cd /home/veto/webs/github/couchdb_sync_c
```

Confirm you see files like:

```sh
ls
```

You should see at least: `CMakeLists.txt`, `include/`, `src/`, `examples/`, `ask.sh`, `docs/`.

---

## Step 2: Build the library (and included examples)

You need the compiled library before building/running an app that links to it.

### Option A: Use the task runner (recommended)

```sh
./ask.sh
```

Then choose:
- `1` — Configure + build
- or `4` — Configure + build (shared library)

### Option B: Use CMake directly

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
```

### What this produces

After a successful build, `build/` should contain:
- `libcouchdb_sync_c.a` (default static library) or `libcouchdb_sync_c.so` (if built shared)
- `couchdb_sync_example` (from `examples/pull_once.c`)
- `simple_sync` (from `examples/simple_sync.c`, added for this tutorial)
- `tests/test_api` (test binary)

> If you change build configuration later (e.g. static/shared), re-run configure+build (task `1` or `4`).

---

## Step 3: Prepare a CouchDB database for testing

You need a CouchDB database with some data to make pull visible.

### 3.1 Check CouchDB is reachable

```sh
curl http://127.0.0.1:5984/
```

You should get a JSON response from CouchDB.

### 3.2 Create a database (if needed)

```sh
curl -X PUT http://127.0.0.1:5984/mydb
```

If the database already exists, CouchDB may return an error indicating that; you can continue.

### 3.3 Add a few sample documents

```sh
curl -X POST http://127.0.0.1:5984/mydb \
  -H 'Content-Type: application/json' \
  -d '{"_id":"item_1","type":"note","text":"hello from couchdb"}'

curl -X POST http://127.0.0.1:5984/mydb \
  -H 'Content-Type: application/json' \
  -d '{"_id":"item_2","type":"note","text":"second document"}'
```

If your server requires authentication, add `-u username:password` to the `curl` commands.

---

## Step 4: Understand the library API you will use

The app uses the public API from `include/couchdb_sync.h`.

### 4.1 Include the header

```c
#include "couchdb_sync.h"
```

### 4.2 Configure the sync context

```c
couchdb_sync_config config = {0};
config.server_url = "http://127.0.0.1:5984";
config.db_name = "mydb";
config.sqlite_path = "local.sqlite3";
config.username = NULL; /* or "user" */
config.password = NULL; /* or "pass" */
```

Required fields: `server_url`, `db_name`, `sqlite_path`.

### 4.3 Open, run operation, close

```c
couchdb_sync *sync = NULL;

if (couchdb_sync_open(&config, &sync) != 0) {
    /* handle error */
}

/* call couchdb_sync_pull / couchdb_sync_push / couchdb_sync_sync */

couchdb_sync_close(sync);
```

### 4.4 Read results

For pull or push:

```c
couchdb_sync_result result = {0};
int rc = couchdb_sync_pull(sync, &result);
/* result.docs_read, result.docs_written, result.last_seq */
free((void *)result.last_seq);
```

For bidirectional sync:

```c
couchdb_sync_bidi_result result = {0};
int rc = couchdb_sync_sync(sync, &result);
/* result.push.*, result.pull.* */
free((void *)result.push.last_seq);
free((void *)result.pull.last_seq);
```

- Functions return `0` on success, non-zero on failure.
- `last_seq` is heap-allocated when returned; free it when present.

For full details, see [`docs/api.md`](api.md).

---

## Step 5: Create the example app source code

The complete app is already provided at `examples/simple_sync.c`. To learn the structure, review it (or recreate it yourself).

### 5.1 Full source (`examples/simple_sync.c`)

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "couchdb_sync.h"

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s <pull|push|sync> <server_url> <db_name> <sqlite_path> [username] [password]\n", prog);
}

static void print_result(const char *label, const couchdb_sync_result *result) {
    printf("%s: docs_read=%ld docs_written=%ld last_seq=%s\n",
           label,
           result->docs_read,
           result->docs_written,
           result->last_seq ? result->last_seq : "(null)");
}

int main(int argc, char **argv) {
    couchdb_sync_config config = {0};
    couchdb_sync *sync = NULL;
    const char *mode;

    if (argc != 5 && argc != 7) {
        print_usage(argv[0]);
        return 1;
    }

    mode = argv[1];
    config.server_url = argv[2];
    config.db_name = argv[3];
    config.sqlite_path = argv[4];
    if (argc == 7) {
        config.username = argv[5];
        config.password = argv[6];
    }

    if (strcmp(mode, "pull") != 0 && strcmp(mode, "push") != 0 && strcmp(mode, "sync") != 0) {
        fprintf(stderr, "unknown mode: %s\n", mode);
        print_usage(argv[0]);
        return 1;
    }

    if (couchdb_sync_open(&config, &sync) != 0) {
        fprintf(stderr, "failed to open sync context\n");
        return 1;
    }

    if (strcmp(mode, "pull") == 0) {
        couchdb_sync_result result = {0};
        if (couchdb_sync_pull(sync, &result) != 0) {
            fprintf(stderr, "pull failed\n");
            free((void *)result.last_seq);
            couchdb_sync_close(sync);
            return 1;
        }
        print_result("pull", &result);
        free((void *)result.last_seq);
    } else if (strcmp(mode, "push") == 0) {
        couchdb_sync_result result = {0};
        if (couchdb_sync_push(sync, &result) != 0) {
            fprintf(stderr, "push failed\n");
            free((void *)result.last_seq);
            couchdb_sync_close(sync);
            return 1;
        }
        print_result("push", &result);
        free((void *)result.last_seq);
    } else {
        couchdb_sync_bidi_result result = {0};
        if (couchdb_sync_sync(sync, &result) != 0) {
            fprintf(stderr, "sync failed\n");
            free((void *)result.push.last_seq);
            free((void *)result.pull.last_seq);
            couchdb_sync_close(sync);
            return 1;
        }
        print_result("push", &result.push);
        print_result("pull", &result.pull);
        free((void *)result.push.last_seq);
        free((void *)result.pull.last_seq);
    }

    couchdb_sync_close(sync);
    return 0;
}
```

### 5.2 What the app does

1. Parses CLI arguments (mode, server URL, DB name, SQLite path, optional credentials)
2. Fills `couchdb_sync_config`
3. Opens a sync context with `couchdb_sync_open()`
4. Runs the selected operation (`pull`, `push`, or `sync`)
5. Prints read/written counts and `last_seq`
6. Frees returned `last_seq` strings and closes the context

---

## Step 6: Build the app

You have two common ways to build this app.

### Option A: Build as part of the project CMake build (easiest)

`CMakeLists.txt` in this project already includes a target for `examples/simple_sync.c`. If you ran the build in Step 2, the binary should already exist at:

```text
build/simple_sync
```

To (re)build everything including this app:

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
```

Or use `ask.sh` task `1` (configure+build) / `2` (compile only).

### Option B: Compile the app manually against the built library

From the project root (after the library is built):

```sh
cc -Iinclude examples/simple_sync.c \
  -Lbuild -lcouchdb_sync_c -lcurl -lsqlite3 \
  -o simple_sync
```

Notes:
- `-Iinclude` provides access to `couchdb_sync.h`.
- `-Lbuild -lcouchdb_sync_c` links the library produced in `build/`.
- `-lcurl -lsqlite3` links runtime dependencies used by the library.
- If you built a shared library (`libcouchdb_sync_c.so`), you may need to set `LD_LIBRARY_PATH=build` when running a manually linked binary, depending on your setup.

---

## Step 7: Run the app (pull)

Run a pull from CouchDB into a local SQLite file:

```sh
./build/simple_sync pull http://127.0.0.1:5984 mydb ./local.sqlite3
```

If the project example target was built in `build/`, the binary is `./build/simple_sync`. If you compiled manually to the current directory, run `./simple_sync`.

### Expected output (example)

```text
pull: docs_read=2 docs_written=2 last_seq=...
```

- `docs_read`: number of remote change items processed
- `docs_written`: number of documents stored locally
- `last_seq`: remote checkpoint sequence (format depends on CouchDB)

### Run pull again (incremental behavior)

```sh
./build/simple_sync pull http://127.0.0.1:5984 mydb ./local.sqlite3
```

If nothing changed remotely, you should typically see `docs_read=0 docs_written=0` with the same/latest checkpoint sequence.

---

## Step 8: Inspect the local SQLite database

The local database file (`local.sqlite3` in the example) is created/updated by the library. You can inspect it with `sqlite3`:

```sh
sqlite3 ./local.sqlite3 ".tables"
```

Expected tables (MVP schema, aligned with PouchDB websql-core naming):

- `metadata-store`
- `document-store`
- `by-sequence`
- `local-store`
- `attach-store`
- `attach-seq-store`

### View stored revision rows

```sh
sqlite3 ./local.sqlite3 "SELECT seq, doc_id, rev, deleted FROM 'by-sequence' ORDER BY seq;"
```

### View stored document JSON

```sh
sqlite3 ./local.sqlite3 "SELECT seq, doc_id, rev, json FROM 'by-sequence' ORDER BY seq;"
```

### View checkpoints

Checkpoints are stored as local docs (e.g. `_local/<replication-id>`):

```sh
sqlite3 ./local.sqlite3 "SELECT id, rev, json FROM 'local-store' WHERE id LIKE '_local/%';"
```

You will see separate checkpoint entries for pull and push replication ids once those directions have run.

---

## Step 9: Run the app (push)

Run push from the local SQLite database to CouchDB:

```sh
./build/simple_sync push http://127.0.0.1:5984 mydb ./local.sqlite3
```

### Expected behavior

- The app reads local changes from `by-sequence` since the push checkpoint
- It asks CouchDB which revisions are missing (`_revs_diff`) and uploads missing ones (`_bulk_docs`, `new_edits:false`)
- It prints push counters and the local checkpoint sequence

If there are no new local changes to push, output will often be:

```text
push: docs_read=0 docs_written=0 last_seq=...
```

> Current MVP note: the library does not provide a high-level document write API. Push is meaningful when your local store contains new revision records/changes to replicate. A simple pull-only mirror workflow will usually have nothing additional to push.

---

## Step 10: Run the app (bidirectional sync)

Run both directions with one command:

```sh
./build/simple_sync sync http://127.0.0.1:5984 mydb ./local.sqlite3
```

### Expected output (example)

```text
push: docs_read=0 docs_written=0 last_seq=...
pull: docs_read=0 docs_written=0 last_seq=...
```

The app prints a `push` result line and a `pull` result line.

### How `sync` behaves

- Executes **push first**, then **pull**
- Uses separate replication ids and checkpoints for push and pull (PouchDB-style separation)

This is useful for incremental synchronization runs.

---

## Step 11: Use authentication (optional)

If your CouchDB requires credentials, pass username and password as the 5th and 6th arguments:

```sh
./build/simple_sync pull http://127.0.0.1:5984 mydb ./local.sqlite3 myuser mypassword
```

The app forwards these values to `couchdb_sync_config.username` and `couchdb_sync_config.password`, and the library uses HTTP basic authentication.

---

## Step 12: Build and run the app outside the project directory (optional)

You can copy the example app source to another location and compile it against this library.

### 12.1 Copy the source

```sh
cp /home/veto/webs/github/couchdb_sync_c/examples/simple_sync.c ./simple_sync.c
```

### 12.2 Compile with explicit include/library paths

```sh
cc -I/home/veto/webs/github/couchdb_sync_c/include \
   simple_sync.c \
   -L/home/veto/webs/github/couchdb_sync_c/build -lcouchdb_sync_c -lcurl -lsqlite3 \
   -o simple_sync
```

### 12.3 Run

```sh
./simple_sync pull http://127.0.0.1:5984 mydb ./local.sqlite3
```

If using a shared build and your runtime linker cannot find `libcouchdb_sync_c.so`, set `LD_LIBRARY_PATH` (example):

```sh
LD_LIBRARY_PATH=/home/veto/webs/github/couchdb_sync_c/build \
  ./simple_sync pull http://127.0.0.1:5984 mydb ./local.sqlite3
```

---

## Step 13: Common issues and fixes

### "failed to open sync context"

- Check that `server_url`, `db_name`, and `sqlite_path` are correct/non-empty
- Ensure the process can create/write `sqlite_path` and its directory
- Ensure required libraries/dependencies are available

### "pull failed" / "push failed" / "sync failed"

- Verify CouchDB is reachable: `curl http://127.0.0.1:5984/`
- Verify the database exists: `curl http://127.0.0.1:5984/mydb`
- Add credentials if required
- Check HTTPS/TLS configuration if using `https://`

### Build/link errors

- Missing headers/libraries: install `libcurl` and `sqlite3` development packages
- `cmake --build build` fails because build dir not configured: run configure first (`cmake -S . -B build ...` or `ask.sh` task `1`/`4`)
- Runtime cannot find shared library: use `LD_LIBRARY_PATH` or install the library in a standard location

### "No changes" on repeated runs

This is normal when already synchronized. Checkpoints allow incremental sync and prevent re-reading/re-writing the same changes.

---

## Step 14: Where to go next

- Read the API reference: [`docs/api.md`](api.md)
- Read the general usage guide: [`docs/howtouse.md`](howtouse.md)
- Review PouchDB mapping and implementation status: [`docs/pouchdb-mapping.md`](pouchdb-mapping.md), [`../PLAN.md`](../PLAN.md)
- Extend the example app (e.g. better argument parsing, environment-based config, logging, or multiple named sync profiles)

### Ideas for extending this tutorial app

- Add a `--help` flag and clearer CLI parsing
- Read config from environment variables or a config file
- Print separate summaries and exit codes per failure mode
- Add a subcommand to inspect local checkpoint/document counts from SQLite
- Add integration tests against a real CouchDB instance (run by you, outside the agent)
