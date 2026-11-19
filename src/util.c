#include "internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

char *couchdb_strdup(const char *s) {
    size_t n;
    char *p;
    if (!s) {
        return NULL;
    }
    n = strlen(s) + 1;
    p = (char *)malloc(n);
    if (!p) {
        return NULL;
    }
    memcpy(p, s, n);
    return p;
}

char *couchdb_json_get_string(const char *json, const char *key) {
    const char *p;
    const char *start;
    const char *end;
    size_t kn;
    size_t n;
    char *out;
    char needle[256];

    if (!json || !key) {
        return NULL;
    }
    kn = strlen(key);
    if (kn + 4 >= sizeof(needle)) {
        return NULL;
    }
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    p = strstr(json, needle);
    if (!p) {
        return NULL;
    }
    p += strlen(needle);
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (*p != ':') {
        return NULL;
    }
    p++;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (*p != '"') {
        return NULL;
    }
    start = p + 1;
    end = strchr(start, '"');
    if (!end) {
        return NULL;
    }
    n = (size_t)(end - start);
    out = (char *)malloc(n + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, start, n);
    out[n] = '\0';
    return out;
}

int couchdb_json_get_int(const char *json, const char *key, int fallback) {
    const char *p;
    char needle[256];
    size_t kn;

    if (!json || !key) {
        return fallback;
    }
    kn = strlen(key);
    if (kn + 4 >= sizeof(needle)) {
        return fallback;
    }
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    p = strstr(json, needle);
    if (!p) {
        return fallback;
    }
    p += strlen(needle);
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (*p != ':') {
        return fallback;
    }
    p++;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (*p == '-' || isdigit((unsigned char)*p)) {
        return atoi(p);
    }
    return fallback;
}

char *couchdb_generate_uuid(void) {
    static unsigned seed;
    char *buf = (char *)malloc(33);
    int i;
    if (!buf) {
        return NULL;
    }
    if (!seed) {
        seed = (unsigned)time(NULL) ^ (unsigned)(uintptr_t)&seed;
        srand(seed);
    }
    for (i = 0; i < 32; i++) {
        int v = rand() % 16;
        buf[i] = (char)(v < 10 ? ('0' + v) : ('a' + (v - 10)));
    }
    buf[32] = '\0';
    return buf;
}
