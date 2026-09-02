#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kv.h"
#include "record.h"

int wal_open(wal_t *w, const char *path) {
    snprintf(w->path, sizeof(w->path), "%s", path);
    w->fp = fopen(path, "ab");
    return w->fp ? 0 : -1;
}

void wal_close(wal_t *w) {
    if (w->fp) {
        fclose(w->fp);
        w->fp = NULL;
    }
}

int wal_append_put(wal_t *w, const char *key, const char *val, size_t vlen) {
    if (record_write(w->fp, REC_PUT, key, val, vlen) != 0)
        return -1;
    return fflush(w->fp) == 0 ? 0 : -1;
}

int wal_append_del(wal_t *w, const char *key) {
    if (record_write(w->fp, REC_DEL, key, NULL, 0) != 0)
        return -1;
    return fflush(w->fp) == 0 ? 0 : -1;
}

int wal_replay(const char *path, memtable_t *mt) {
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return 0; /* no WAL yet is not an error */

    char op;
    char *key, *val;
    size_t vlen;
    int rc;
    while ((rc = record_read(fp, &op, &key, &val, &vlen)) == 1) {
        memtable_put(mt, key, op == REC_PUT ? val : NULL, vlen);
        free(key);
        free(val);
    }
    fclose(fp);
    /* rc == -1 means the last record was truncated (a crash mid-write);
     * everything before it already replayed, which is the durability
     * guarantee a WAL actually offers. */
    return 0;
}

int wal_truncate(const char *path) {
    FILE *fp = fopen(path, "wb");
    if (!fp)
        return -1;
    fclose(fp);
    return 0;
}
