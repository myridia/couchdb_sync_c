#include "internal.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} couchdb_buf;

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    couchdb_buf *buf = (couchdb_buf *)userdata;
    size_t n = size * nmemb;
    size_t need = buf->len + n + 1;
    if (need > buf->cap) {
        size_t cap = buf->cap ? buf->cap * 2 : 4096;
        char *tmp;
        while (cap < need) cap *= 2;
        tmp = (char *)realloc(buf->data, cap);
        if (!tmp) return 0;
        buf->data = tmp;
        buf->cap = cap;
    }
    memcpy(buf->data + buf->len, ptr, n);
    buf->len += n;
    buf->data[buf->len] = '\0';
    return n;
}

int couchdb_http_init(couchdb_http *http, const couchdb_sync_config *config) {
    if (!http || !config) return -1;
    memset(http, 0, sizeof(*http));
    http->server_url = couchdb_strdup(config->server_url);
    http->username = config->username ? couchdb_strdup(config->username) : NULL;
    http->password = config->password ? couchdb_strdup(config->password) : NULL;
    http->timeout_ms = 30000;
    curl_global_init(CURL_GLOBAL_DEFAULT);
    return 0;
}

void couchdb_http_destroy(couchdb_http *http) {
    if (!http) return;
    free(http->server_url);
    free(http->username);
    free(http->password);
    memset(http, 0, sizeof(*http));
}

static char *join_url(const char *base, const char *db, const char *path_and_query) {
    size_t n;
    char *out;
    const char *sep = "/";
    if (!base || !db || !path_and_query) return NULL;
    n = strlen(base) + 1 + strlen(db) + strlen(path_and_query) + 2;
    out = (char *)malloc(n);
    if (!out) return NULL;
    if (base[strlen(base) - 1] == '/') sep = "";
    snprintf(out, n, "%s%s%s%s", base, sep, db, path_and_query);
    return out;
}

int couchdb_http_request_json(couchdb_http *http, const char *method, const char *path, const char *body, char **out, long *status) {
    CURL *curl;
    CURLcode rc;
    couchdb_buf buf = {0};
    struct curl_slist *headers = NULL;
    char *url;
    long code = 0;

    if (out) *out = NULL;
    if (status) *status = 0;
    if (!http || !http->server_url || !method || !path) return -1;

    url = join_url(http->server_url, "", path);
    if (!url) return -1;

    curl = curl_easy_init();
    if (!curl) {
        free(url);
        return -1;
    }

    headers = curl_slist_append(headers, "Accept: application/json");
    if (body) {
        headers = curl_slist_append(headers, "Content-Type: application/json");
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, http->timeout_ms);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    if (http->username) {
        curl_easy_setopt(curl, CURLOPT_USERNAME, http->username);
        if (http->password) curl_easy_setopt(curl, CURLOPT_PASSWORD, http->password);
        curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
    }
    if (body) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    }

    rc = curl_easy_perform(curl);
    if (rc == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(url);

    if (rc != CURLE_OK) {
        free(buf.data);
        return -1;
    }
    if (status) *status = code;
    if (out) *out = buf.data;
    else free(buf.data);
    return 0;
}

int couchdb_http_get_db_info(couchdb_http *http, const char *db_name, char **out) {
    char path[512];
    long status = 0;
    int rc;
    snprintf(path, sizeof(path), "/%s", db_name);
    rc = couchdb_http_request_json(http, "GET", path, NULL, out, &status);
    if (rc != 0 || status >= 400) {
        if (out && *out) { free(*out); *out = NULL; }
        return -1;
    }
    return 0;
}
