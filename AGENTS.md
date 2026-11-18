# AGENTS.md — couchdb_sync_c

## What this is

A C library project to sync documents between a CouchDB server and local SQLite storage, modeled after PouchDB/CouchDB replication concepts.

## Stack

- Language: C (C11 target)
- HTTP: libcurl
- JSON: minimal internal parsing for MVP; replace with a full JSON library for complete handling
- Storage: SQLite3
- Build: CMake

## Structure

- `include/` — public API headers
- `src/` — implementation (`couchdb_sync`, `http_curl`, `store_sqlite`, `checkpointer`, `replication_id`, `sync_pull`, `sync_push`, `rev`, `util`, `internal.h`)
- `examples/` — sample usage
- `tests/` — CTest-based tests
- `ask.sh`, `ask.d/` — task runner (`1` build, `2` test)
- `docs/` — design/reference notes (`pouchdb-mapping.md`)
- `PLAN.md` — mapping from PouchDB replication/storage to C + SQLite

## APIs

- `couchdb_sync_pull()` remote → local
- `couchdb_sync_push()` local → remote
- `couchdb_sync_sync()` bidirectional (push then pull), with separate checkpoints/replication ids

## Conventions

- Follow workspace `~/webs/agents/AGENTS.md` conventions (no git commands, no builds/checks by agent, literal naming, no comments unless asked).
- Keep storage behind an interface so sync logic is independent from SQLite schema details.
- Preserve CouchDB document fields (`_id`, `_rev`, `_deleted`) and checkpoint state explicitly.
- MVP scope: pull/push/sync with resumable checkpoints; extend later for revision-tree/full replication behavior, attachments, and live sync.

## Notes

- Local PouchDB clone used as reference: `../pouchdb` (modules: replication, checkpointer, generate-replication-id, adapter-http, adapter-websql-core, adapter-utils, merge).
- This is a reimplementation in C, not a JS port.
- Agent does not build/test; user runs CMake/build on host.
