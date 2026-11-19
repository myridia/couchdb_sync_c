# PouchDB mapping notes

## Local clone reference

- `/home/veto/webs/github/pouchdb/packages/node_modules/pouchdb-replication/src/replicate.js`
- `/home/veto/webs/github/pouchdb/packages/node_modules/pouchdb-replication/src/getDocs.js`
- `/home/veto/webs/github/pouchdb/packages/node_modules/pouchdb-checkpointer/src/index.js`
- `/home/veto/webs/github/pouchdb/packages/node_modules/pouchdb-generate-replication-id/src/index.js`
- `/home/veto/webs/github/pouchdb/packages/node_modules/pouchdb-adapter-websql-core/src/constants.js`
- `/home/veto/webs/github/pouchdb/packages/node_modules/pouchdb-adapter-websql-core/src/index.js`
- `/home/veto/webs/github/pouchdb/packages/node_modules/pouchdb-adapter-websql-core/src/bulkDocs.js`
- `/home/veto/webs/github/pouchdb/packages/node_modules/pouchdb-adapter-utils/src/parseDoc.js`
- `/home/veto/webs/github/pouchdb/packages/node_modules/pouchdb-adapter-utils/src/processDocs.js`
- `/home/veto/webs/github/pouchdb/packages/node_modules/pouchdb-adapter-utils/src/updateDoc.js`
- `/home/veto/webs/github/pouchdb/packages/node_modules/pouchdb-merge/src/{merge,winningRev,collectLeaves,...}.js`

## Replication core

- `replicate.js`: reads changes in batches, tracks pending/completed batches, computes missing revs via `revsDiff`, fetches docs via `getDocs` (bulk/open_revs, attachments), writes with `bulkDocs({new_edits:false})`, and writes checkpoints after committed batches.
- `checkpointer`: stores checkpoints as local docs (`_local/<replication-id>`) with `last_seq`, `session_id`, `version`, `replicator`, and bounded history; compares source/target sessions/history to resume safely.
- `getDocs`: fetches current/missing revisions and attachment data from source (reusing target attachment data when valid).

## Storage model to mirror

- websql-core stores: `metadata-store`, `document-store`, `by-sequence`, `attach-store`, `local-store`, `attach-seq-store` plus indexes.
- Normal docs keep metadata/revision state and sequence linkage; local docs are separate and not replicated.
- Full correctness requires rev-tree/merge semantics (`parseDoc`, `merge`, `winningRev`, `updateDoc/processDocs`).

## C implementation status

- `src/store_sqlite.c`: schema and indexes aligned with websql-core store names; local doc and checkpoint persistence; `by-sequence` + `document-store` upserts; local change feed helpers for push.
- `src/checkpointer.c`: checkpoint get/write in `local-store` for direction-specific replication ids.
- `src/replication_id.c`: deterministic, direction-sensitive MVP replication id generation (pull vs push).
- `src/sync_pull.c`: checkpointed pull loop using `GET /_changes?style=all_docs&include_docs=true`, transactional apply, checkpoint advance (MVP path).
- `src/sync_push.c`: checkpointed push loop using local `by-sequence` changes, `POST /_revs_diff`, and `POST /_bulk_docs` with `new_edits:false` (MVP path).
- `src/couchdb_sync.c`: separated APIs like PouchDB direction ops — `couchdb_sync_pull`, `couchdb_sync_push`, `couchdb_sync_sync` (push then pull).
- `src/rev.c`: rev parsing foundation for upcoming merge logic.
