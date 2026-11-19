#include "internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *build_bulk_docs_body(couchdb_local_change *changes, int *send, int count) {
    size_t cap = 256;
    size_t len = 0;
    char *buf = (char *)malloc(cap);
    int i;
    int first = 1;
    if (!buf) return NULL;
    len += (size_t)snprintf(buf + len, cap - len, "{\"new_edits\":false,\"docs\":[");
    for (i = 0; i < count; i++) {
        const char *doc;
        size_t need;
        if (!send[i]) continue;
        doc = changes[i].json ? changes[i].json : "{}";
        need = len + strlen(doc) + 8;
        if (need > cap) {
            char *tmp;
            while (cap < need) cap *= 2;
            tmp = (char *)realloc(buf, cap);
            if (!tmp) { free(buf); return NULL; }
            buf = tmp;
        }
        if (!first) {
            buf[len++] = ',';
            buf[len] = '\0';
        }
        first = 0;
        len += (size_t)snprintf(buf + len, cap - len, "%s", doc);
    }
    if (len + 4 > cap) {
        char *tmp = (char *)realloc(buf, len + 4);
        if (!tmp) { free(buf); return NULL; }
        buf = tmp;
        cap = len + 4;
    }
    snprintf(buf + len, cap - len, "]}");
    return buf;
}

static char *build_revs_diff_body(couchdb_local_change *changes, int count) {
    size_t cap = 256;
    size_t len = 0;
    char *buf = (char *)malloc(cap);
    int i;
    int first = 1;
    if (!buf) return NULL;
    buf[0] = '{';
    buf[1] = '\0';
    len = 1;
    for (i = 0; i < count; i++) {
        size_t need;
        char *id = changes[i].doc_id ? changes[i].doc_id : "";
        char *rev = changes[i].rev ? changes[i].rev : "";
        need = len + strlen(id) + strlen(rev) + 16;
        if (need > cap) {
            char *tmp;
            while (cap < need) cap *= 2;
            tmp = (char *)realloc(buf, cap);
            if (!tmp) { free(buf); return NULL; }
            buf = tmp;
        }
        if (!first) {
            buf[len++] = ',';
            buf[len] = '\0';
        }
        first = 0;
        len += (size_t)snprintf(buf + len, cap - len, "\"%s\":[\"%s\"]", id, rev);
    }
    if (len + 2 > cap) {
        char *tmp = (char *)realloc(buf, len + 2);
        if (!tmp) { free(buf); return NULL; }
        buf = tmp;
        cap = len + 2;
    }
    buf[len++] = '}';
    buf[len] = '\0';
    return buf;
}

static int rev_is_missing_in_response(const char *resp, const char *id, const char *rev) {
    const char *p = resp;
    const char *idpos;
    const char *obj;
    const char *missing;
    const char *end;
    char needle[512];
    size_t n;
    if (!resp || !id || !rev) return 1;
    snprintf(needle, sizeof(needle), "\"%s\"", id);
    idpos = strstr(p, needle);
    if (!idpos) return 0;
    obj = strchr(idpos + strlen(needle), '{');
    if (!obj) return 0;
    missing = strstr(obj, "\"missing\"");
    if (!missing) return 0;
    missing = strchr(missing, '[');
    if (!missing) return 0;
    end = strchr(missing, ']');
    if (!end) return 0;
    n = (size_t)(end - missing + 1);
    {
        char *arr = (char *)malloc(n + 1);
        char revq[256];
        int found;
        if (!arr) return 1;
        memcpy(arr, missing, n);
        arr[n] = '\0';
        snprintf(revq, sizeof(revq), "\"%s\"", rev);
        found = strstr(arr, revq) != NULL;
        free(arr);
        return found;
    }
}

int couchdb_sync_push_run(couchdb_sync *sync, couchdb_sync_result *result) {
    char *since_str = NULL;
    sqlite3_int64 since = 0;
    long docs_read = 0;
    long docs_written = 0;
    int done = 0;

    if (!sync) return -1;
    if (result) {
        result->docs_read = 0;
        result->docs_written = 0;
        result->last_seq = NULL;
    }

    if (couchdb_checkpointer_get(&sync->store, sync->push_replication_id, &since_str) != 0) {
        since_str = couchdb_strdup("0");
    }
    if (!since_str) since_str = couchdb_strdup("0");
    since = (sqlite3_int64)atoll(since_str);

    while (!done) {
        couchdb_local_change *changes = NULL;
        int count = 0;
        int *send = NULL;
        char *diff_body = NULL;
        char *diff_resp = NULL;
        char *bulk_body = NULL;
        char *bulk_resp = NULL;
        long status = 0;
        char path[512];
        char seq_buf[64];
        sqlite3_int64 last_seq_num = 0;
        int i;
        int to_send = 0;

        if (couchdb_store_get_changes_since(&sync->store, since, 100, &changes, &count) != 0) {
            free(since_str);
            return -1;
        }
        if (count == 0) {
            couchdb_store_free_changes(changes, count);
            done = 1;
            break;
        }

        docs_read += count;
        last_seq_num = changes[count - 1].seq;
        send = (int *)calloc((size_t)count, sizeof(int));
        if (!send) {
            couchdb_store_free_changes(changes, count);
            free(since_str);
            return -1;
        }

        diff_body = build_revs_diff_body(changes, count);
        if (diff_body) {
            snprintf(path, sizeof(path), "/%s/_revs_diff", sync->config.db_name);
            if (couchdb_http_request_json(&sync->http, "POST", path, diff_body, &diff_resp, &status) == 0 && status < 400 && diff_resp) {
                for (i = 0; i < count; i++) {
                    if (rev_is_missing_in_response(diff_resp, changes[i].doc_id, changes[i].rev)) {
                        send[i] = 1;
                        to_send++;
                    }
                }
            } else {
                for (i = 0; i < count; i++) { send[i] = 1; to_send++; }
            }
        } else {
            for (i = 0; i < count; i++) { send[i] = 1; to_send++; }
        }

        if (to_send > 0) {
            bulk_body = build_bulk_docs_body(changes, send, count);
            if (!bulk_body) {
                free(diff_body); free(diff_resp); free(send);
                couchdb_store_free_changes(changes, count);
                free(since_str);
                return -1;
            }
            snprintf(path, sizeof(path), "/%s/_bulk_docs", sync->config.db_name);
            status = 0;
            if (couchdb_http_request_json(&sync->http, "POST", path, bulk_body, &bulk_resp, &status) != 0 || status >= 400) {
                free(bulk_body); free(bulk_resp);
                free(diff_body); free(diff_resp); free(send);
                couchdb_store_free_changes(changes, count);
                free(since_str);
                return -1;
            }
            docs_written += to_send;
            free(bulk_body);
            free(bulk_resp);
        }

        snprintf(seq_buf, sizeof(seq_buf), "%lld", (long long)last_seq_num);
        if (couchdb_checkpointer_write(&sync->store, sync->push_replication_id, sync->session_id, seq_buf) != 0) {
            free(diff_body); free(diff_resp); free(send);
            couchdb_store_free_changes(changes, count);
            free(since_str);
            return -1;
        }

        since = last_seq_num;
        free(since_str);
        since_str = couchdb_strdup(seq_buf);

        free(diff_body);
        free(diff_resp);
        free(send);
        couchdb_store_free_changes(changes, count);
    }

    if (result) {
        result->docs_read = docs_read;
        result->docs_written = docs_written;
        result->last_seq = since_str;
    } else {
        free(since_str);
    }
    return 0;
}
