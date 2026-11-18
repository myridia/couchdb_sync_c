# API Reference

This document describes the public C interface exposed by `include/couchdb_sync.h`.

## Header

```c
#include "couchdb_sync.h"
```

The header is C and C++ compatible (`extern "C"` when included from C++).

## Types

### `couchdb_sync`

```c
typedef struct couchdb_sync couchdb_sync;
```

Opaque handle representing an open sync context. It holds:
- resolved configuration (server URL, database name, SQLite path, optional credentials)
- HTTP client state (libcurl)
- local SQLite store handle
- direction-specific replication ids (pull and push)
- a session id used for checkpoint writes

Create with `couchdb_sync_open()` and destroy with `couchdb_sync_close()`.

### `couchdb_sync_config`

```c
typedef struct {
    const char *server_url;
    const char *db_name;
    const char *sqlite_path;
    const char *username;
    const char *password;
} couchdb_sync_config;
```

Configuration for opening a sync context.

| Field | Required | Description |
|---|---|---|
| `server_url` | yes | CouchDB server base URL, e.g. `http://127.0.0.1:5984` or `https://db.example.com` |
| `db_name` | yes | Remote database name, e.g. `mydb` |
| `sqlite_path` | yes | Path to local SQLite database file (created/opened by the library) |
| `username` | no | HTTP basic auth username (`NULL` to disable) |
| `password` | no | HTTP basic auth password |

Notes:
- `server_url`, `db_name`, and `sqlite_path` must be non-`NULL`.
- String values are copied by `couchdb_sync_open()`; the caller retains ownership of the input strings.
- Basic auth is applied when `username` is provided.

### `couchdb_sync_result`

```c
typedef struct {
    long docs_read;
    long docs_written;
    const char *last_seq;
} couchdb_sync_result;
```

Result of a one-direction replication run (`pull` or `push`).

| Field | Description |
|---|---|
| `docs_read` | Number of change entries/documents read from the source side during the run |
| `docs_written` | Number of documents written/applied to the target side during the run |
| `last_seq` | Final checkpoint sequence for the run (heap-allocated string), or `NULL` on failure/when unavailable |

Semantics by direction:
- **Pull:** `docs_read` counts remote change items processed; `docs_written` counts docs stored locally; `last_seq` is the remote CouchDB changes sequence checkpoint.
- **Push:** `docs_read` counts local `by-sequence` change rows scanned; `docs_written` counts docs uploaded to CouchDB in `_bulk_docs` batches (after `_revs_diff` filtering); `last_seq` is the local SQLite sequence checkpoint (string form).

Ownership/lifetime:
- On success with a non-`NULL` `result`, `last_seq` points to memory allocated by the library. The caller is responsible for releasing it with `free((void *)result->last_seq)` when no longer needed.
- If `result` is passed as `NULL`, the library does not expose `last_seq`.
- `docs_read` and `docs_written` are set to `0` at the start of a run when `result` is non-`NULL`.

### `couchdb_sync_bidi_result`

```c
typedef struct {
    couchdb_sync_result push;
    couchdb_sync_result pull;
} couchdb_sync_bidi_result;
```

Combined result for `couchdb_sync_sync()`.

| Field | Description |
|---|---|
| `push` | Result of the push phase (local → remote) |
| `pull` | Result of the pull phase (remote → local) |

Ownership/lifetime:
- If `result` is non-`NULL`, each of `push.last_seq` and `pull.last_seq` may be heap-allocated and must be freed by the caller when present.
- If `result` is `NULL`, the library frees internal sequence strings.

## Functions

### `couchdb_sync_open`

```c
int couchdb_sync_open(const couchdb_sync_config *config, couchdb_sync **out);
```

Opens and initializes a sync context.

Behavior:
1. Validates required config fields (`server_url`, `db_name`, `sqlite_path`) and `out`.
2. Copies configuration strings.
3. Initializes HTTP client state (libcurl globals and per-context settings).
4. Opens/creates the SQLite database at `sqlite_path`.
5. Initializes the local schema (tables and indexes aligned with PouchDB websql-core store names).
6. Computes direction-specific replication ids for pull and push.
7. Generates a session id for checkpoint writes.
8. Returns an opaque handle via `*out` on success.

Parameters:
- `config`: pointer to configuration (must remain valid only for the duration of the call; values are copied)
- `out`: receives the created handle on success

Returns:
- `0` on success
- non-zero on failure (invalid arguments, allocation failure, HTTP/store initialization failure, etc.)

### `couchdb_sync_pull`

```c
int couchdb_sync_pull(couchdb_sync *sync, couchdb_sync_result *result);
```

Replicates changes from the remote CouchDB database to the local SQLite store (remote → local), resuming from the stored pull checkpoint.

Behavior (MVP):
1. Reads pull checkpoint (`last_seq`) from local store using the pull replication id; defaults to `"0"` if none.
2. Requests changes in batches: `GET /{db}/_changes?feed=normal&style=all_docs&since=<seq>&limit=100&include_docs=true`.
3. For each change item, extracts document id/rev/deleted and included doc JSON and stores it locally.
4. Applies each batch in a local transaction and writes the updated pull checkpoint.
5. Continues until a batch yields no processable results/end condition, then returns final counters and `last_seq`.

Parameters:
- `sync`: open sync handle
- `result`: optional output result; may be `NULL`

Returns:
- `0` on success
- non-zero on failure (HTTP/request error, parse/storage/checkpoint failure)

### `couchdb_sync_push`

```c
int couchdb_sync_push(couchdb_sync *sync, couchdb_sync_result *result);
```

Replicates local changes from SQLite to the remote CouchDB database (local → remote), resuming from the stored push checkpoint.

Behavior (MVP):
1. Reads push checkpoint (local sequence, stored as string) from local store using the push replication id; defaults to `"0"` if none.
2. Reads local changes from `by-sequence` in batches (`seq > since`, ordered ascending).
3. Builds `_revs_diff` request for batch revisions and calls `POST /{db}/_revs_diff`.
4. Uploads missing revisions via `POST /{db}/_bulk_docs` with `{"new_edits": false, "docs": [...]}`.
5. Writes updated push checkpoint after successful batch handling.
6. Continues until no more local changes are found.

Parameters:
- `sync`: open sync handle
- `result`: optional output result; may be `NULL`

Returns:
- `0` on success
- non-zero on failure

Notes:
- If `_revs_diff` cannot be completed/parsed for a batch, the implementation falls back to attempting upload of batch docs.
- Pushed document bodies are taken from stored local revision JSON.

### `couchdb_sync_sync`

```c
int couchdb_sync_sync(couchdb_sync *sync, couchdb_sync_bidi_result *result);
```

Runs bidirectional synchronization using separated directional operations.

Behavior:
1. Runs `push` (local → remote).
2. Runs `pull` (remote → local).
3. Returns combined results in `result` when provided.

Parameters:
- `sync`: open sync handle
- `result`: optional combined output; may be `NULL`

Returns:
- `0` only if both push and pull succeed
- non-zero if either direction fails (results for executed phases may still be populated in `result`)

Checkpointing:
- Pull and push use different replication ids and therefore independent checkpoints, similar to PouchDB running two replications for `sync`.

### `couchdb_sync_close`

```c
void couchdb_sync_close(couchdb_sync *sync);
```

Closes and frees a sync context.

Behavior:
- closes the SQLite store
- releases HTTP/config/session/replication-id resources
- frees the handle

Safe to call with `NULL`.

## Local Storage Model (MVP)

The SQLite schema is initialized by `couchdb_sync_open()` and is modeled after PouchDB's websql-core stores:

- `metadata-store`
- `document-store`
- `by-sequence` (+ indexes)
- `local-store` (used for checkpoints as `_local/<replication-id>`)
- `attach-store`
- `attach-seq-store` (+ indexes)

Current replication behavior stores/pushes document revision records and updates document metadata in a simplified way suitable for MVP. Full PouchDB-style revision-tree/conflict semantics are not yet implemented.

## Error Handling and Limitations

- Functions return `0` on success and non-zero on failure; there is no detailed public error code/message API yet.
- HTTP failures and non-2xx responses are treated as replication failures for the affected run.
- JSON handling is minimal and purpose-built for current replication paths; a full JSON library is planned for robustness.
- Pull currently uses `changes` with `include_docs=true` (MVP path), not a full `revsDiff`/`bulkGet` (or `open_revs`) flow.
- Attachments, full rev-tree merge/winning/conflict logic, live/continuous replication, retry/backoff, filtered/selector replication, and remote checkpoint coordination are not fully implemented.
- The library is not thread-safe unless externally synchronized.

## Usage Examples

### Pull (remote → local)

```c
#include <stdio.h>
#include <stdlib.h>
#include "couchdb_sync.h"

int main(void) {
    couchdb_sync_config config = {0};
    couchdb_sync *sync = NULL;
    couchdb_sync_result result = {0};

    config.server_url = "http://127.0.0.1:5984";
    config.db_name = "example";
    config.sqlite_path = "example.sqlite3";

    if (couchdb_sync_open(&config, &sync) != 0) return 1;
    if (couchdb_sync_pull(sync, &result) != 0) {
        couchdb_sync_close(sync);
        return 1;
    }

    printf("read=%ld written=%ld last_seq=%s\n",
           result.docs_read, result.docs_written,
           result.last_seq ? result.last_seq : "(null)");

    free((void *)result.last_seq);
    couchdb_sync_close(sync);
    return 0;
}
```

### Push (local → remote)

```c
couchdb_sync_result res = {0};
if (couchdb_sync_push(sync, &res) == 0) {
    /* use res.docs_read, res.docs_written, res.last_seq */
    free((void *)res.last_seq);
}
```

### Bidirectional sync

```c
couchdb_sync_bidi_result res = {0};
if (couchdb_sync_sync(sync, &res) == 0) {
    /* res.push and res.pull populated */
}
free((void *)res.push.last_seq);
free((void *)res.pull.last_seq);
```
