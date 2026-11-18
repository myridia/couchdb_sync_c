<img src="couchdb_sync_c.svg" alt="couchdb_sync_c logo" width="120">

# couchdb_sync_c

C library for syncing documents between a CouchDB server and a local SQLite database, reimplementing PouchDB/CouchDB replication and storage concepts in C.

## Status

Implementation scaffold (MVP), using the local PouchDB clone at `../pouchdb` as reference.

## APIs

Separated replication APIs, similar to PouchDB's direction-specific operations:
- `couchdb_sync_pull()` — replicate remote CouchDB → local SQLite
- `couchdb_sync_push()` — replicate local SQLite → remote CouchDB
- `couchdb_sync_sync()` — bidirectional sync (push then pull, with separate checkpoints)

## Dependencies

- `libcurl` for HTTP
- `sqlite3` for local storage
- JSON parsing: current code uses minimal internal parsing for MVP fields; a full JSON library can replace it for complete document handling

## Build and test

Requires CMake, a C compiler, `libcurl`, and `sqlite3` development packages.

- Configure and build: `cmake -S . -B build -DBUILD_TESTING=ON && cmake --build build`
- Run tests: `ctest --test-dir build --output-on-failure`
- Task runner: `./ask.sh` (or `sh ask.sh`), with tasks in `ask.d/` (`1` build, `2` test)

## Layout

- `include/` public headers
- `src/` implementation (`http_curl`, `store_sqlite`, `checkpointer`, `replication_id`, `sync_pull`, `sync_push`, `rev`, `util`)
- `examples/` usage examples
- `tests/` basic API tests (CTest)
- `ask.sh`, `ask.d/` task runner
- `docs/` design/reference notes
- `PLAN.md` PouchDB-to-C/SQLite mapping

## Reference (local)

Primary PouchDB modules consulted in `../pouchdb/packages/node_modules/`:
- `pouchdb-replication/src/replicate.js`, `getDocs.js`, `sync.js`, `replicateWrapper.js`, `replication.js`
- `pouchdb-checkpointer/src/index.js`
- `pouchdb-generate-replication-id/src/index.js`
- `pouchdb-adapter-http/src/index.js`
- `pouchdb-adapter-websql-core/src/{index,constants,utils,bulkDocs}.js`
- `pouchdb-adapter-utils/src/{parseDoc,processDocs,updateDoc}.js`
- `pouchdb-merge/src/{merge,winningRev,collectLeaves,...}.js`
