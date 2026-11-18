#include "internal.h"

#include <stdlib.h>
#include <string.h>

int couchdb_sync_open(const couchdb_sync_config *config, couchdb_sync **out) {
    couchdb_sync *sync;
    int rc;

    if (!config || !config->server_url || !config->db_name || !config->sqlite_path || !out) {
        return -1;
    }

    sync = (couchdb_sync *)calloc(1, sizeof(*sync));
    if (!sync) {
        return -1;
    }

    sync->config.server_url = couchdb_strdup(config->server_url);
    sync->config.db_name = couchdb_strdup(config->db_name);
    sync->config.sqlite_path = couchdb_strdup(config->sqlite_path);
    sync->config.username = config->username ? couchdb_strdup(config->username) : NULL;
    sync->config.password = config->password ? couchdb_strdup(config->password) : NULL;
    sync->revs_limit = 1000;

    rc = couchdb_http_init(&sync->http, &sync->config);
    if (rc != 0) {
        couchdb_sync_close(sync);
        return -1;
    }
    rc = couchdb_store_open(&sync->store, sync->config.sqlite_path);
    if (rc != 0) {
        couchdb_sync_close(sync);
        return -1;
    }
    rc = couchdb_store_init(&sync->store);
    if (rc != 0) {
        couchdb_sync_close(sync);
        return -1;
    }
    rc = couchdb_replication_id(sync, 0, &sync->pull_replication_id);
    if (rc != 0) {
        couchdb_sync_close(sync);
        return -1;
    }
    rc = couchdb_replication_id(sync, 1, &sync->push_replication_id);
    if (rc != 0) {
        couchdb_sync_close(sync);
        return -1;
    }
    sync->session_id = couchdb_generate_uuid();
    if (!sync->session_id) {
        couchdb_sync_close(sync);
        return -1;
    }

    *out = sync;
    return 0;
}

int couchdb_sync_pull(couchdb_sync *sync, couchdb_sync_result *result) {
    if (!sync) {
        return -1;
    }
    return couchdb_sync_pull_run(sync, result);
}

int couchdb_sync_push(couchdb_sync *sync, couchdb_sync_result *result) {
    if (!sync) {
        return -1;
    }
    return couchdb_sync_push_run(sync, result);
}

int couchdb_sync_sync(couchdb_sync *sync, couchdb_sync_bidi_result *result) {
    int rc_push;
    int rc_pull;
    couchdb_sync_bidi_result local;

    if (!sync) {
        return -1;
    }
    memset(&local, 0, sizeof(local));
    rc_push = couchdb_sync_push_run(sync, &local.push);
    rc_pull = couchdb_sync_pull_run(sync, &local.pull);
    if (result) {
        *result = local;
    } else {
        free((void *)local.push.last_seq);
        free((void *)local.pull.last_seq);
    }
    if (rc_push != 0 || rc_pull != 0) {
        return -1;
    }
    return 0;
}

void couchdb_sync_close(couchdb_sync *sync) {
    if (!sync) return;
    couchdb_store_close(&sync->store);
    couchdb_http_destroy(&sync->http);
    free((void *)sync->config.server_url);
    free((void *)sync->config.db_name);
    free((void *)sync->config.sqlite_path);
    free((void *)sync->config.username);
    free((void *)sync->config.password);
    free(sync->source_id);
    free(sync->target_id);
    free(sync->pull_replication_id);
    free(sync->push_replication_id);
    free(sync->session_id);
    free(sync);
}
