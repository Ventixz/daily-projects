#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "kv.h"

static void wal_path(const kv_store_t *db, char *out, size_t out_sz) {
    snprintf(out, out_sz, "%s/wal.log", db->dir);
}

static int by_id(const void *a, const void *b) {
    return (int)((const sstable_t *)a)->id - (int)((const sstable_t *)b)->id;
}

int kv_open(kv_store_t *db, const char *dir) {
    snprintf(db->dir, sizeof(db->dir), "%s", dir);
    mkdir(dir, 0755); /* EEXIST is fine, everything else surfaces on the opens below */

    memtable_init(&db->mt);
    db->sstables = NULL;
    db->sstable_count = 0;
    db->next_sstable_id = 0;

    DIR *d = opendir(dir);
    if (!d)
        return -1;

    struct dirent *ent;
    size_t cap = 0;
    while ((ent = readdir(d)) != NULL) {
        unsigned id;
        if (sscanf(ent->d_name, "%6u.sst", &id) != 1)
            continue;

        if (db->sstable_count == cap) {
            cap = cap ? cap * 2 : 4;
            db->sstables = realloc(db->sstables, cap * sizeof(sstable_t));
        }
        char path[600];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        if (sstable_open(path, id, &db->sstables[db->sstable_count]) != 0) {
            closedir(d);
            return -1;
        }
        db->sstable_count++;
        if (id + 1 > db->next_sstable_id)
            db->next_sstable_id = id + 1;
    }
    closedir(d);

    qsort(db->sstables, db->sstable_count, sizeof(sstable_t), by_id);

    char wpath[512];
    wal_path(db, wpath, sizeof(wpath));
    if (wal_replay(wpath, &db->mt) != 0)
        return -1;
    if (wal_open(&db->wal, wpath) != 0)
        return -1;

    return 0;
}

void kv_close(kv_store_t *db) {
    wal_close(&db->wal);
    memtable_free(&db->mt);
    for (size_t i = 0; i < db->sstable_count; i++)
        sstable_free(&db->sstables[i]);
    free(db->sstables);
    db->sstables = NULL;
    db->sstable_count = 0;
}

int kv_flush(kv_store_t *db) {
    if (db->mt.len == 0)
        return 0;

    sstable_t new_sst;
    if (sstable_flush(&db->mt, db->dir, db->next_sstable_id, &new_sst) != 0)
        return -1;
    db->next_sstable_id++;

    db->sstables = realloc(db->sstables, (db->sstable_count + 1) * sizeof(sstable_t));
    db->sstables[db->sstable_count++] = new_sst;

    memtable_free(&db->mt);
    memtable_init(&db->mt);

    wal_close(&db->wal);
    char wpath[512];
    wal_path(db, wpath, sizeof(wpath));
    if (wal_truncate(wpath) != 0)
        return -1;
    if (wal_open(&db->wal, wpath) != 0)
        return -1;

    if (db->sstable_count >= KV_COMPACT_THRESHOLD)
        return kv_compact(db);
    return 0;
}

int kv_compact(kv_store_t *db) {
    if (db->sstable_count <= 1)
        return 0;

    sstable_t **inputs = malloc(db->sstable_count * sizeof(sstable_t *));
    for (size_t i = 0; i < db->sstable_count; i++)
        inputs[i] = &db->sstables[i];

    sstable_t merged;
    uint32_t merged_id = db->next_sstable_id++;
    int rc = sstable_compact(inputs, db->sstable_count, db->dir, merged_id, &merged);
    free(inputs);
    if (rc != 0)
        return -1;

    for (size_t i = 0; i < db->sstable_count; i++) {
        char old_path[512];
        snprintf(old_path, sizeof(old_path), "%s", db->sstables[i].path);
        sstable_free(&db->sstables[i]);
        remove(old_path);
    }
    free(db->sstables);

    db->sstables = malloc(sizeof(sstable_t));
    db->sstables[0] = merged;
    db->sstable_count = 1;
    return 0;
}

int kv_put(kv_store_t *db, const char *key, const char *val, size_t vlen) {
    if (wal_append_put(&db->wal, key, val, vlen) != 0)
        return -1;
    memtable_put(&db->mt, key, val, vlen);
    if (db->mt.len >= KV_MEMTABLE_FLUSH_THRESHOLD)
        return kv_flush(db);
    return 0;
}

int kv_del(kv_store_t *db, const char *key) {
    if (wal_append_del(&db->wal, key) != 0)
        return -1;
    memtable_put(&db->mt, key, NULL, 0);
    if (db->mt.len >= KV_MEMTABLE_FLUSH_THRESHOLD)
        return kv_flush(db);
    return 0;
}

int kv_get(kv_store_t *db, const char *key, char **val, size_t *vlen) {
    const char *mval;
    size_t mvlen;
    int tombstone;
    if (memtable_get(&db->mt, key, &mval, &mvlen, &tombstone)) {
        if (tombstone)
            return 0;
        *val = malloc(mvlen ? mvlen : 1);
        memcpy(*val, mval, mvlen);
        *vlen = mvlen;
        return 1;
    }

    for (size_t i = db->sstable_count; i-- > 0;) {
        char *sval;
        size_t svlen;
        int stomb;
        int rc = sstable_get(&db->sstables[i], key, &sval, &svlen, &stomb);
        if (rc < 0)
            return -1;
        if (rc == 1) {
            if (stomb) {
                free(sval);
                return 0;
            }
            *val = sval;
            *vlen = svlen;
            return 1;
        }
    }
    return 0;
}
