#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int couchdb_checkpointer_get(couchdb_store *store, const char *replication_id, char **out_seq) {
    couchdb_checkpoint cp;
    int rc;

    if (out_seq) *out_seq = NULL;
    if (!store || !replication_id) return -1;

    rc = couchdb_store_get_checkpoint(store, replication_id, &cp);
    if (rc == 1) {
        if (out_seq) *out_seq = couchdb_strdup("0");
        return 0;
    }
    if (rc != 0) {
        return -1;
    }
    if (out_seq) {
        if (cp.last_seq && cp.last_seq[0]) {
            *out_seq = couchdb_strdup(cp.last_seq);
        } else {
            *out_seq = couchdb_strdup("0");
        }
    }
    free(cp.id);
    free(cp.last_seq);
    free(cp.session_id);
    free(cp.history_json);
    return 0;
}

int couchdb_checkpointer_write(couchdb_store *store, const char *replication_id, const char *session_id, const char *last_seq) {
    couchdb_checkpoint cp;

    if (!store || !replication_id || !last_seq) return -1;
    memset(&cp, 0, sizeof(cp));
    cp.last_seq = (char *)last_seq;
    cp.session_id = (char *)session_id;
    return couchdb_store_put_checkpoint(store, replication_id, &cp);
}
