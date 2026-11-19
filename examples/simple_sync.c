#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "couchdb_sync.h"

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s <pull|push|sync> <server_url> <db_name> <sqlite_path> [username] [password]\n", prog);
}

static void print_result(const char *label, const couchdb_sync_result *result) {
    printf("%s: docs_read=%ld docs_written=%ld last_seq=%s\n",
           label,
           result->docs_read,
           result->docs_written,
           result->last_seq ? result->last_seq : "(null)");
}

int main(int argc, char **argv) {
    couchdb_sync_config config = {0};
    couchdb_sync *sync = NULL;
    const char *mode;

    if (argc != 5 && argc != 7) {
        print_usage(argv[0]);
        return 1;
    }

    mode = argv[1];
    config.server_url = argv[2];
    config.db_name = argv[3];
    config.sqlite_path = argv[4];
    if (argc == 7) {
        config.username = argv[5];
        config.password = argv[6];
    }

    if (strcmp(mode, "pull") != 0 && strcmp(mode, "push") != 0 && strcmp(mode, "sync") != 0) {
        fprintf(stderr, "unknown mode: %s\n", mode);
        print_usage(argv[0]);
        return 1;
    }

    if (couchdb_sync_open(&config, &sync) != 0) {
        fprintf(stderr, "failed to open sync context\n");
        return 1;
    }

    if (strcmp(mode, "pull") == 0) {
        couchdb_sync_result result = {0};
        if (couchdb_sync_pull(sync, &result) != 0) {
            fprintf(stderr, "pull failed\n");
            free((void *)result.last_seq);
            couchdb_sync_close(sync);
            return 1;
        }
        print_result("pull", &result);
        free((void *)result.last_seq);
    } else if (strcmp(mode, "push") == 0) {
        couchdb_sync_result result = {0};
        if (couchdb_sync_push(sync, &result) != 0) {
            fprintf(stderr, "push failed\n");
            free((void *)result.last_seq);
            couchdb_sync_close(sync);
            return 1;
        }
        print_result("push", &result);
        free((void *)result.last_seq);
    } else {
        couchdb_sync_bidi_result result = {0};
        if (couchdb_sync_sync(sync, &result) != 0) {
            fprintf(stderr, "sync failed\n");
            free((void *)result.push.last_seq);
            free((void *)result.pull.last_seq);
            couchdb_sync_close(sync);
            return 1;
        }
        print_result("push", &result.push);
        print_result("pull", &result.pull);
        free((void *)result.push.last_seq);
        free((void *)result.pull.last_seq);
    }

    couchdb_sync_close(sync);
    return 0;
}
