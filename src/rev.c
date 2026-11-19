#include "internal.h"

#include <stdlib.h>
#include <string.h>

int couchdb_parse_rev(const char *rev, int *prefix, const char **id) {
    const char *dash;
    if (!rev || !prefix || !id) return -1;
    dash = strchr(rev, '-');
    if (!dash || dash == rev || *(dash + 1) == '\0') return -1;
    *prefix = atoi(rev);
    *id = dash + 1;
    return *prefix > 0 ? 0 : -1;
}
