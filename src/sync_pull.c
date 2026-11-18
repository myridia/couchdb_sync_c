#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static char *extract_json_object(const char *s, const char **endp) {
    int depth = 0;
    int in_str = 0;
    int esc = 0;
    const char *p = s;
    const char *start = NULL;
    while (*p) {
        char c = *p++;
        if (!start) {
            if (isspace((unsigned char)c)) continue;
            if (c == '{') { start = p - 1; depth = 1; }
            else return NULL;
            continue;
        }
        if (in_str) {
            if (esc) esc = 0;
            else if (c == '\\') esc = 1;
            else if (c == '"') in_str = 0;
            continue;
        }
        if (c == '"') in_str = 1;
        else if (c == '{') depth++;
        else if (c == '}') {
            depth--;
            if (depth == 0) {
                size_t n = (size_t)(p - start);
                char *out = (char *)malloc(n + 1);
                if (!out) return NULL;
                memcpy(out, start, n);
                out[n] = '\0';
                if (endp) *endp = p;
                return out;
            }
        }
    }
    return NULL;
}

static const char *find_key(const char *json, const char *key) {
    char needle[256];
    size_t kn = strlen(key);
    if (kn + 4 >= sizeof(needle)) return NULL;
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    return strstr(json, needle);
}

static char *json_get_string_value(const char *json, const char *key) {
    const char *p = find_key(json, key);
    const char *start;
    const char *q;
    size_t n;
    char *out;
    if (!p) return NULL;
    p += strlen(key) + 2;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p != ':') return NULL;
    p++;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p == '"') {
        start = p + 1;
        q = strchr(start, '"');
        if (!q) return NULL;
        n = (size_t)(q - start);
        out = (char *)malloc(n + 1);
        if (!out) return NULL;
        memcpy(out, start, n);
        out[n] = '\0';
        return out;
    }
    if (strncmp(p, "null", 4) == 0) return couchdb_strdup("0");
    if (*p == '-' || isdigit((unsigned char)*p)) {
        q = p;
        while (*q == '-' || isdigit((unsigned char)*q)) q++;
        n = (size_t)(q - p);
        out = (char *)malloc(n + 1);
        if (!out) return NULL;
        memcpy(out, p, n);
        out[n] = '\0';
        return out;
    }
    return NULL;
}

static int json_get_bool(const char *json, const char *key, int fallback) {
    const char *p = find_key(json, key);
    if (!p) return fallback;
    p += strlen(key) + 2;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p != ':') return fallback;
    p++;
    while (*p && isspace((unsigned char)*p)) p++;
    if (strncmp(p, "true", 4) == 0) return 1;
    if (strncmp(p, "false", 5) == 0) return 0;
    return fallback;
}

static char *find_results_array(const char *json) {
    const char *p = strstr(json, "\"results\"");
    if (!p) return NULL;
    p = strchr(p, '[');
    return (char *)p;
}

int couchdb_sync_pull_run(couchdb_sync *sync, couchdb_sync_result *result) {
    char *since = NULL;
    char *last_seq = NULL;
    int done = 0;
    long docs_read = 0;
    long docs_written = 0;

    if (!sync) return -1;
    if (result) {
        result->docs_read = 0;
        result->docs_written = 0;
        result->last_seq = NULL;
    }

    if (couchdb_checkpointer_get(&sync->store, sync->pull_replication_id, &since) != 0) {
        since = couchdb_strdup("0");
    }
    if (!since) since = couchdb_strdup("0");

    while (!done) {
        char path[2048];
        char *resp = NULL;
        long status = 0;
        char *arr;
        const char *p;
        int batch_count = 0;

        snprintf(path, sizeof(path),
            "/%s/_changes?feed=normal&style=all_docs&since=%s&limit=100&include_docs=true",
            sync->config.db_name, since ? since : "0");

        if (couchdb_http_request_json(&sync->http, "GET", path, NULL, &resp, &status) != 0 || status >= 400 || !resp) {
            free(resp);
            free(since);
            free(last_seq);
            return -1;
        }

        free(last_seq);
        last_seq = json_get_string_value(resp, "last_seq");
        if (!last_seq) last_seq = couchdb_strdup(since);

        arr = find_results_array(resp);
        p = arr ? arr + 1 : NULL;
        if (couchdb_store_begin(&sync->store) != 0) {
            free(resp);
            free(since);
            free(last_seq);
            return -1;
        }

        while (p && *p) {
            char *item;
            const char *end;
            char *id;
            char *rev = NULL;
            char *doc;
            int deleted;
            const char *changes_p;
            const char *doc_p;

            while (*p && (isspace((unsigned char)*p) || *p == ',')) p++;
            if (*p == ']') break;
            item = extract_json_object(p, &end);
            if (!item) break;
            p = end;
            batch_count++;

            id = json_get_string_value(item, "id");
            deleted = json_get_bool(item, "deleted", 0);
            changes_p = strstr(item, "\"changes\"");
            if (changes_p) {
                changes_p = strchr(changes_p, '[');
                if (changes_p) {
                    const char *e2;
                    char *chg = extract_json_object(changes_p + 1, &e2);
                    if (chg) {
                        rev = json_get_string_value(chg, "rev");
                        free(chg);
                    }
                }
            }
            doc_p = strstr(item, "\"doc\"");
            doc = NULL;
            if (doc_p) {
                doc_p = strchr(doc_p, '{');
                if (doc_p) {
                    const char *e3;
                    doc = extract_json_object(doc_p, &e3);
                }
            }

            docs_read++;
            if (id && rev && doc) {
                if (couchdb_store_put_doc(&sync->store, id, rev, deleted, doc) == 0) {
                    docs_written++;
                }
            } else if (id && rev) {
                char minimal[1024];
                snprintf(minimal, sizeof(minimal),
                    "{\"_id\":\"%s\",\"_rev\":\"%s\"%s}", id, rev, deleted ? ",\"_deleted\":true" : "");
                if (couchdb_store_put_doc(&sync->store, id, rev, deleted, minimal) == 0) {
                    docs_written++;
                }
            }

            free(id);
            free(rev);
            free(doc);
            free(item);
        }

        if (couchdb_checkpointer_write(&sync->store, sync->pull_replication_id, sync->session_id, last_seq ? last_seq : since) != 0) {
            couchdb_store_rollback(&sync->store);
            free(resp);
            free(since);
            free(last_seq);
            return -1;
        }
        if (couchdb_store_commit(&sync->store) != 0) {
            free(resp);
            free(since);
            free(last_seq);
            return -1;
        }

        free(since);
        since = couchdb_strdup(last_seq ? last_seq : "0");
        if (!arr || batch_count == 0) {
            done = 1;
        }
        free(resp);
    }

    if (result) {
        result->docs_read = docs_read;
        result->docs_written = docs_written;
        result->last_seq = since;
    } else {
        free(since);
    }
    free(last_seq);
    return 0;
}
