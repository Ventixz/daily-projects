#include <stdlib.h>
#include <string.h>

#include "kv.h"
#include "record.h"

/* Writes mt's entries to dir/%06u.sst, building the in-memory index as it
 * goes. If include_tombstones is 0, deleted keys are simply omitted (only
 * safe once there is no older sstable left that a tombstone could still be
 * shadowing, i.e. after a full compaction). */
static int write_sstable(const memtable_t *mt, const char *dir, uint32_t id, int include_tombstones, sstable_t *out) {
    snprintf(out->path, sizeof(out->path), "%s/%06u.sst", dir, id);
    out->id = id;

    FILE *fp = fopen(out->path, "wb");
    if (!fp)
        return -1;

    out->index = malloc(mt->len ? mt->len * sizeof(sst_index_entry_t) : sizeof(sst_index_entry_t));
    out->count = 0;

    for (size_t i = 0; i < mt->len; i++) {
        const kv_entry_t *e = &mt->entries[i];
        if (e->val == NULL && !include_tombstones)
            continue;

        long offset = ftell(fp);
        char op = e->val ? REC_PUT : REC_DEL;
        if (record_write(fp, op, e->key, e->val, e->vlen) != 0) {
            fclose(fp);
            sstable_free(out);
            return -1;
        }
        out->index[out->count].key = strdup(e->key);
        out->index[out->count].offset = (uint64_t)offset;
        out->count++;
    }

    fclose(fp);
    return 0;
}

int sstable_flush(const memtable_t *mt, const char *dir, uint32_t id, sstable_t *out) {
    return write_sstable(mt, dir, id, /*include_tombstones=*/1, out);
}

int sstable_open(const char *path, uint32_t id, sstable_t *out) {
    snprintf(out->path, sizeof(out->path), "%s", path);
    out->id = id;
    out->index = NULL;
    out->count = 0;

    FILE *fp = fopen(path, "rb");
    if (!fp)
        return -1;

    size_t cap = 8;
    out->index = malloc(cap * sizeof(sst_index_entry_t));

    char op;
    char *key, *val;
    size_t vlen;
    long offset = ftell(fp);
    int rc;
    while ((rc = record_read(fp, &op, &key, &val, &vlen)) == 1) {
        if (out->count == cap) {
            cap *= 2;
            out->index = realloc(out->index, cap * sizeof(sst_index_entry_t));
        }
        out->index[out->count].key = key; /* record_read already malloc'd it */
        out->index[out->count].offset = (uint64_t)offset;
        out->count++;
        free(val);
        offset = ftell(fp);
    }
    fclose(fp);
    return rc == -1 ? -1 : 0;
}

void sstable_free(sstable_t *s) {
    for (size_t i = 0; i < s->count; i++)
        free(s->index[i].key);
    free(s->index);
    s->index = NULL;
    s->count = 0;
}

int sstable_get(const sstable_t *s, const char *key, char **val, size_t *vlen, int *is_tombstone) {
    size_t lo = 0, hi = s->count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int cmp = strcmp(s->index[mid].key, key);
        if (cmp < 0)
            lo = mid + 1;
        else if (cmp > 0)
            hi = mid;
        else {
            FILE *fp = fopen(s->path, "rb");
            if (!fp)
                return -1;
            fseek(fp, (long)s->index[mid].offset, SEEK_SET);

            char op;
            char *k, *v;
            size_t vl;
            int rc = record_read(fp, &op, &k, &v, &vl);
            fclose(fp);
            if (rc != 1) {
                free(k);
                return -1;
            }
            free(k);

            *is_tombstone = (op == REC_DEL);
            *val = v;
            *vlen = vl;
            return 1;
        }
    }
    return 0;
}

int sstable_compact(sstable_t **inputs, size_t n, const char *dir, uint32_t id, sstable_t *out) {
    memtable_t merged;
    memtable_init(&merged);

    /* Applying oldest-to-newest lets memtable_put's overwrite-on-duplicate
     * behavior do the "newest version wins" resolution for us. */
    for (size_t i = 0; i < n; i++) {
        memtable_t tmp;
        memtable_init(&tmp);
        if (wal_replay(inputs[i]->path, &tmp) != 0) {
            memtable_free(&tmp);
            memtable_free(&merged);
            return -1;
        }
        for (size_t j = 0; j < tmp.len; j++) {
            const kv_entry_t *e = &tmp.entries[j];
            memtable_put(&merged, e->key, e->val, e->vlen);
        }
        memtable_free(&tmp);
    }

    int rc = write_sstable(&merged, dir, id, /*include_tombstones=*/0, out);
    memtable_free(&merged);
    return rc;
}
