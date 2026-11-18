#include <stdio.h>

#include "couchdb_sync.h"

int main(void) {
    couchdb_sync_config config = {0};
    couchdb_sync *sync = NULL;
    couchdb_sync_result result = {0};

    config.server_url = "http://127.0.0.1:5984";
    config.db_name = "example";
    config.sqlite_path = "example.sqlite3";

    if (couchdb_sync_open(&config, &sync) != 0) {
        fprintf(stderr, "open failed\n");
        return 1;
    }

    if (couchdb_sync_pull(sync, &result) != 0) {
        fprintf(stderr, "pull failed\n");
        couchdb_sync_close(sync);
        return 1;
    }

    printf("docs_read=%ld docs_written=%ld\n", result.docs_read, result.docs_written);
    couchdb_sync_close(sync);
    return 0;
}
