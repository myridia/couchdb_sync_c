#ifndef COUCHDB_SYNC_H
#define COUCHDB_SYNC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct couchdb_sync couchdb_sync;

typedef struct {
    const char *server_url;
    const char *db_name;
    const char *sqlite_path;
    const char *username;
    const char *password;
} couchdb_sync_config;

typedef struct {
    long docs_read;
    long docs_written;
    const char *last_seq;
} couchdb_sync_result;

typedef struct {
    couchdb_sync_result push;
    couchdb_sync_result pull;
} couchdb_sync_bidi_result;

int couchdb_sync_open(const couchdb_sync_config *config, couchdb_sync **out);
int couchdb_sync_pull(couchdb_sync *sync, couchdb_sync_result *result);
int couchdb_sync_push(couchdb_sync *sync, couchdb_sync_result *result);
int couchdb_sync_sync(couchdb_sync *sync, couchdb_sync_bidi_result *result);
void couchdb_sync_close(couchdb_sync *sync);

#ifdef __cplusplus
}
#endif

#endif
