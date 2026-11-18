#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fnv1a_update(unsigned long long *h, const char *s) {
    const unsigned char *p = (const unsigned char *)(s ? s : "");
    while (*p) {
        *h ^= (unsigned long long)*p++;
        *h *= 1099511628211ULL;
    }
    *h ^= 0xffULL;
    *h *= 1099511628211ULL;
}

int couchdb_replication_id(couchdb_sync *sync, int push, char **out_id) {
    unsigned long long h = 1469598103934665603ULL;
    char buf[32];
    const char *remote;
    const char *local;
    char remote_buf[1024];

    if (!sync || !out_id) return -1;
    snprintf(remote_buf, sizeof(remote_buf), "%s/%s", sync->config.server_url ? sync->config.server_url : "", sync->config.db_name ? sync->config.db_name : "");
    remote = remote_buf;
    local = sync->config.sqlite_path ? sync->config.sqlite_path : "";

    if (push) {
        fnv1a_update(&h, local);
        fnv1a_update(&h, "\0");
        fnv1a_update(&h, remote);
        fnv1a_update(&h, "\0push");
    } else {
        fnv1a_update(&h, remote);
        fnv1a_update(&h, "\0");
        fnv1a_update(&h, local);
        fnv1a_update(&h, "\0pull");
    }

    snprintf(buf, sizeof(buf), "%016llx", h);
    *out_id = couchdb_strdup(buf);
    return *out_id ? 0 : -1;
}
