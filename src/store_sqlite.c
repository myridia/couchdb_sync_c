#include "internal.h"

#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int exec_sql(sqlite3 *db, const char *sql) {
    char *err = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        sqlite3_free(err);
        return -1;
    }
    return 0;
}

int couchdb_store_open(couchdb_store *store, const char *path) {
    sqlite3 *db = NULL;
    if (!store || !path) {
        return -1;
    }
    if (sqlite3_open(path, &db) != SQLITE_OK) {
        if (db) {
            sqlite3_close(db);
        }
        return -1;
    }
    store->db = db;
    store->path = couchdb_strdup(path);
    return 0;
}

int couchdb_store_init(couchdb_store *store) {
    sqlite3 *db;
    if (!store || !store->db) {
        return -1;
    }
    db = (sqlite3 *)store->db;

    if (exec_sql(db, "PRAGMA journal_mode=WAL") != 0) {
        return -1;
    }
    if (exec_sql(db, "BEGIN") != 0) {
        return -1;
    }

    if (exec_sql(db,
        "CREATE TABLE IF NOT EXISTS 'metadata-store' ("
        "dbid TEXT PRIMARY KEY, db_version INTEGER, max_seq INTEGER)"
    ) != 0) goto fail;

    if (exec_sql(db,
        "CREATE TABLE IF NOT EXISTS 'document-store' ("
        "id TEXT UNIQUE, json TEXT, winningseq INTEGER, max_seq INTEGER)"
    ) != 0) goto fail;

    if (exec_sql(db,
        "CREATE TABLE IF NOT EXISTS 'by-sequence' ("
        "seq INTEGER PRIMARY KEY AUTOINCREMENT, json TEXT, deleted INTEGER, doc_id TEXT, rev TEXT)"
    ) != 0) goto fail;

    if (exec_sql(db,
        "CREATE INDEX IF NOT EXISTS 'by-seq-deleted-idx' ON 'by-sequence'(seq, deleted)"
    ) != 0) goto fail;

    if (exec_sql(db,
        "CREATE INDEX IF NOT EXISTS 'by-seq-doc-id-rev' ON 'by-sequence'(doc_id, rev)"
    ) != 0) goto fail;

    if (exec_sql(db,
        "CREATE INDEX IF NOT EXISTS 'doc-winningseq-idx' ON 'document-store'(winningseq)"
    ) != 0) goto fail;

    if (exec_sql(db,
        "CREATE TABLE IF NOT EXISTS 'local-store' ("
        "id TEXT PRIMARY KEY, rev TEXT, json TEXT)"
    ) != 0) goto fail;

    if (exec_sql(db,
        "CREATE TABLE IF NOT EXISTS 'attach-store' ("
        "digest TEXT PRIMARY KEY, json TEXT, body BLOB)"
    ) != 0) goto fail;

    if (exec_sql(db,
        "CREATE TABLE IF NOT EXISTS 'attach-seq-store' ("
        "seq INTEGER, digest TEXT)"
    ) != 0) goto fail;

    if (exec_sql(db,
        "CREATE INDEX IF NOT EXISTS 'attach-seq-seq-idx' ON 'attach-seq-store'(seq)"
    ) != 0) goto fail;

    if (exec_sql(db,
        "CREATE UNIQUE INDEX IF NOT EXISTS 'attach-seq-digest-idx' ON 'attach-seq-store'(digest, seq)"
    ) != 0) goto fail;

    if (exec_sql(db, "COMMIT") != 0) {
        return -1;
    }
    return 0;

fail:
    exec_sql(db, "ROLLBACK");
    return -1;
}

int couchdb_store_begin(couchdb_store *store) {
    if (!store || !store->db) return -1;
    return exec_sql((sqlite3 *)store->db, "BEGIN IMMEDIATE");
}

int couchdb_store_commit(couchdb_store *store) {
    if (!store || !store->db) return -1;
    return exec_sql((sqlite3 *)store->db, "COMMIT");
}

int couchdb_store_rollback(couchdb_store *store) {
    if (!store || !store->db) return -1;
    return exec_sql((sqlite3 *)store->db, "ROLLBACK");
}

int couchdb_store_get_local(couchdb_store *store, const char *id, char **out_rev, char **out_json) {
    sqlite3 *db;
    sqlite3_stmt *stmt = NULL;
    int rc;
    const unsigned char *rev;
    const unsigned char *json;

    if (out_rev) *out_rev = NULL;
    if (out_json) *out_json = NULL;
    if (!store || !store->db || !id) return -1;
    db = (sqlite3 *)store->db;

    rc = sqlite3_prepare_v2(db, "SELECT rev, json FROM 'local-store' WHERE id = ?", -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, id, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        rev = sqlite3_column_text(stmt, 0);
        json = sqlite3_column_text(stmt, 1);
        if (out_rev && rev) *out_rev = couchdb_strdup((const char *)rev);
        if (out_json && json) *out_json = couchdb_strdup((const char *)json);
        sqlite3_finalize(stmt);
        return 0;
    }
    sqlite3_finalize(stmt);
    return 1;
}

int couchdb_store_put_local(couchdb_store *store, const char *id, const char *rev, const char *json) {
    sqlite3 *db;
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (!store || !store->db || !id || !json) return -1;
    db = (sqlite3 *)store->db;

    rc = sqlite3_prepare_v2(db,
        "INSERT INTO 'local-store'(id, rev, json) VALUES(?, ?, ?) "
        "ON CONFLICT(id) DO UPDATE SET rev=excluded.rev, json=excluded.json",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, id, -1, SQLITE_TRANSIENT);
    if (rev) sqlite3_bind_text(stmt, 2, rev, -1, SQLITE_TRANSIENT);
    else sqlite3_bind_null(stmt, 2);
    sqlite3_bind_text(stmt, 3, json, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? 0 : -1;
}

int couchdb_store_get_checkpoint(couchdb_store *store, const char *replication_id, couchdb_checkpoint *out) {
    char local_id[512];
    char *rev = NULL;
    char *json = NULL;
    int rc;

    if (!out || !replication_id) return -1;
    memset(out, 0, sizeof(*out));
    snprintf(local_id, sizeof(local_id), "_local/%s", replication_id);
    rc = couchdb_store_get_local(store, local_id, &rev, &json);
    if (rc != 0) {
        return rc;
    }
    out->id = couchdb_strdup(local_id);
    out->last_seq = couchdb_json_get_string(json, "last_seq");
    out->session_id = couchdb_json_get_string(json, "session_id");
    out->history_json = json;
    free(rev);
    return 0;
}

int couchdb_store_put_checkpoint(couchdb_store *store, const char *replication_id, const couchdb_checkpoint *cp) {
    char local_id[512];
    char rev_buf[64];
    char *body;
    int n;
    int rc;

    if (!store || !replication_id || !cp) return -1;
    snprintf(local_id, sizeof(local_id), "_local/%s", replication_id);
    snprintf(rev_buf, sizeof(rev_buf), "0-%u", (unsigned)(cp->last_seq ? strlen(cp->last_seq) : 1));
    n = snprintf(NULL, 0,
        "{\"_id\":\"%s\",\"last_seq\":\"%s\",\"session_id\":\"%s\",\"version\":1,\"replicator\":\"pouchdb\"}",
        local_id,
        cp->last_seq ? cp->last_seq : "0",
        cp->session_id ? cp->session_id : "");
    if (n < 0) return -1;
    body = (char *)malloc((size_t)n + 1);
    if (!body) return -1;
    snprintf(body, (size_t)n + 1,
        "{\"_id\":\"%s\",\"last_seq\":\"%s\",\"session_id\":\"%s\",\"version\":1,\"replicator\":\"pouchdb\"}",
        local_id,
        cp->last_seq ? cp->last_seq : "0",
        cp->session_id ? cp->session_id : "");
    rc = couchdb_store_put_local(store, local_id, rev_buf, body);
    free(body);
    return rc;
}

void couchdb_store_close(couchdb_store *store) {
    if (!store) return;
    if (store->db) {
        sqlite3_close((sqlite3 *)store->db);
        store->db = NULL;
    }
    free(store->path);
    store->path = NULL;
}

int couchdb_store_put_doc(couchdb_store *store, const char *id, const char *rev, int deleted, const char *json) {
    sqlite3 *db;
    sqlite3_stmt *stmt = NULL;
    sqlite3_int64 seq = 0;
    int rc;
    char meta_json[1024];

    if (!store || !store->db || !id || !rev || !json) return -1;
    db = (sqlite3 *)store->db;

    rc = sqlite3_prepare_v2(db,
        "INSERT INTO 'by-sequence'(json, deleted, doc_id, rev) VALUES(?, ?, ?, ?)",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, json, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, deleted ? 1 : 0);
    sqlite3_bind_text(stmt, 3, id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, rev, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return -1;

    seq = sqlite3_last_insert_rowid(db);
    snprintf(meta_json, sizeof(meta_json),
        "{\"id\":\"%s\",\"rev\":\"%s\",\"deleted\":%s,\"seq\":%lld}",
        id, rev, deleted ? "true" : "false", (long long)seq);

    rc = sqlite3_prepare_v2(db,
        "INSERT INTO 'document-store'(id, json, winningseq, max_seq) VALUES(?, ?, ?, ?) "
        "ON CONFLICT(id) DO UPDATE SET json=excluded.json, winningseq=excluded.winningseq, "
        "max_seq=CASE WHEN excluded.max_seq > 'document-store'.max_seq THEN excluded.max_seq ELSE 'document-store'.max_seq END",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, meta_json, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, seq);
    sqlite3_bind_int64(stmt, 4, seq);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? 0 : -1;
}

int couchdb_store_get_changes_since(couchdb_store *store, sqlite3_int64 since, int limit, couchdb_local_change **out, int *out_count) {
    sqlite3 *db;
    sqlite3_stmt *stmt = NULL;
    int rc;
    int cap = 0;
    int count = 0;
    couchdb_local_change *items = NULL;

    if (out) *out = NULL;
    if (out_count) *out_count = 0;
    if (!store || !store->db || !out || !out_count) return -1;
    db = (sqlite3 *)store->db;

    rc = sqlite3_prepare_v2(db,
        "SELECT seq, doc_id, rev, deleted, json FROM 'by-sequence' "
        "WHERE seq > ? ORDER BY seq ASC LIMIT ?",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;
    sqlite3_bind_int64(stmt, 1, since);
    sqlite3_bind_int(stmt, 2, limit > 0 ? limit : 100);

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        couchdb_local_change *tmp;
        if (count == cap) {
            cap = cap ? cap * 2 : 16;
            tmp = (couchdb_local_change *)realloc(items, (size_t)cap * sizeof(*items));
            if (!tmp) {
                sqlite3_finalize(stmt);
                couchdb_store_free_changes(items, count);
                return -1;
            }
            items = tmp;
        }
        items[count].seq = sqlite3_column_int64(stmt, 0);
        items[count].doc_id = couchdb_strdup((const char *)sqlite3_column_text(stmt, 1));
        items[count].rev = couchdb_strdup((const char *)sqlite3_column_text(stmt, 2));
        items[count].deleted = sqlite3_column_int(stmt, 3);
        items[count].json = couchdb_strdup((const char *)sqlite3_column_text(stmt, 4));
        count++;
    }
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        couchdb_store_free_changes(items, count);
        return -1;
    }
    *out = items;
    *out_count = count;
    return 0;
}

void couchdb_store_free_changes(couchdb_local_change *changes, int count) {
    int i;
    if (!changes) return;
    for (i = 0; i < count; i++) {
        free(changes[i].doc_id);
        free(changes[i].rev);
        free(changes[i].json);
    }
    free(changes);
}

sqlite3_int64 couchdb_store_last_seq(couchdb_store *store) {
    sqlite3 *db;
    sqlite3_stmt *stmt = NULL;
    sqlite3_int64 seq = 0;
    if (!store || !store->db) return 0;
    db = (sqlite3 *)store->db;
    if (sqlite3_prepare_v2(db, "SELECT COALESCE(MAX(seq), 0) FROM 'by-sequence'", -1, &stmt, NULL) != SQLITE_OK) {
        return 0;
    }
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        seq = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return seq;
}
