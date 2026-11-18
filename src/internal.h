#ifndef COUCHDB_SYNC_INTERNAL_H
#define COUCHDB_SYNC_INTERNAL_H

#include "couchdb_sync.h"

#include <sqlite3.h>
#include <stddef.h>
#include <stdint.h>

typedef struct couchdb_http {
    char *server_url;
    char *username;
    char *password;
    long timeout_ms;
} couchdb_http;

typedef struct couchdb_store {
    void *db;
    char *path;
} couchdb_store;

typedef struct couchdb_checkpoint {
    char *id;
    char *last_seq;
    char *session_id;
    char *history_json;
} couchdb_checkpoint;

typedef struct couchdb_changes_result {
    char *last_seq;
    char **doc_ids;
    char **revs;
    char **changes_json;
    size_t count;
} couchdb_changes_result;

typedef struct {
    sqlite3_int64 seq;
    char *doc_id;
    char *rev;
    int deleted;
    char *json;
} couchdb_local_change;

struct couchdb_sync {
    couchdb_sync_config config;
    couchdb_http http;
    couchdb_store store;
    char *source_id;
    char *target_id;
    char *pull_replication_id;
    char *push_replication_id;
    char *session_id;
    int revs_limit;
};

int couchdb_http_init(couchdb_http *http, const couchdb_sync_config *config);
void couchdb_http_destroy(couchdb_http *http);
int couchdb_http_request_json(couchdb_http *http, const char *method, const char *path, const char *body, char **out, long *status);

int couchdb_store_open(couchdb_store *store, const char *path);
int couchdb_store_init(couchdb_store *store);
int couchdb_store_get_local(couchdb_store *store, const char *id, char **out_rev, char **out_json);
int couchdb_store_put_local(couchdb_store *store, const char *id, const char *rev, const char *json);
int couchdb_store_get_checkpoint(couchdb_store *store, const char *replication_id, couchdb_checkpoint *out);
int couchdb_store_put_checkpoint(couchdb_store *store, const char *replication_id, const couchdb_checkpoint *cp);
int couchdb_store_begin(couchdb_store *store);
int couchdb_store_commit(couchdb_store *store);
int couchdb_store_rollback(couchdb_store *store);
int couchdb_store_put_doc(couchdb_store *store, const char *id, const char *rev, int deleted, const char *json);
int couchdb_store_get_changes_since(couchdb_store *store, sqlite3_int64 since, int limit, couchdb_local_change **out, int *out_count);
void couchdb_store_free_changes(couchdb_local_change *changes, int count);
sqlite3_int64 couchdb_store_last_seq(couchdb_store *store);
void couchdb_store_close(couchdb_store *store);

int couchdb_checkpointer_get(couchdb_store *store, const char *replication_id, char **out_seq);
int couchdb_checkpointer_write(couchdb_store *store, const char *replication_id, const char *session_id, const char *last_seq);

int couchdb_replication_id(couchdb_sync *sync, int push, char **out_id);
int couchdb_sync_pull_run(couchdb_sync *sync, couchdb_sync_result *result);
int couchdb_sync_push_run(couchdb_sync *sync, couchdb_sync_result *result);

char *couchdb_strdup(const char *s);
char *couchdb_json_get_string(const char *json, const char *key);
int couchdb_json_get_int(const char *json, const char *key, int fallback);
char *couchdb_generate_uuid(void);

#endif
