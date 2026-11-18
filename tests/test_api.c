#include <stdio.h>

#include "couchdb_sync.h"

int main(void) {
    couchdb_sync *sync = NULL;
    couchdb_sync_config config = {0};
    int rc;

    rc = couchdb_sync_open(NULL, &sync);
    if (rc == 0) {
        fprintf(stderr, "expected failure for NULL config\n");
        return 1;
    }

    config.server_url = "http://127.0.0.1:5984";
    config.db_name = "example";
    config.sqlite_path = NULL;
    rc = couchdb_sync_open(&config, &sync);
    if (rc == 0) {
        fprintf(stderr, "expected failure for NULL sqlite_path\n");
        return 1;
    }

    printf("ok\n");
    return 0;
}
