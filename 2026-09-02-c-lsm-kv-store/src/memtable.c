#include <stdlib.h>
#include <string.h>

#include "kv.h"

void memtable_init(memtable_t *mt) {
    mt->entries = NULL;
    mt->len = 0;
    mt->cap = 0;
}

void memtable_free(memtable_t *mt) {
    for (size_t i = 0; i < mt->len; i++) {
        free(mt->entries[i].key);
        free(mt->entries[i].val);
    }
    free(mt->entries);
    mt->entries = NULL;
    mt->len = mt->cap = 0;
}

/* Lower bound: first index whose key is >= key. */
static size_t lower_bound(const memtable_t *mt, const char *key) {
    size_t lo = 0, hi = mt->len;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (strcmp(mt->entries[mid].key, key) < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

void memtable_put(memtable_t *mt, const char *key, const char *val, size_t vlen) {
    size_t idx = lower_bound(mt, key);

    if (idx < mt->len && strcmp(mt->entries[idx].key, key) == 0) {
        free(mt->entries[idx].val);
        mt->entries[idx].val = val ? malloc(vlen ? vlen : 1) : NULL;
        if (val && vlen)
            memcpy(mt->entries[idx].val, val, vlen);
        mt->entries[idx].vlen = val ? vlen : 0;
        return;
    }

    if (mt->len == mt->cap) {
        mt->cap = mt->cap ? mt->cap * 2 : 8;
        mt->entries = realloc(mt->entries, mt->cap * sizeof(kv_entry_t));
    }
    memmove(&mt->entries[idx + 1], &mt->entries[idx], (mt->len - idx) * sizeof(kv_entry_t));

    mt->entries[idx].key = strdup(key);
    if (val) {
        mt->entries[idx].val = malloc(vlen ? vlen : 1);
        if (vlen)
            memcpy(mt->entries[idx].val, val, vlen);
        mt->entries[idx].vlen = vlen;
    } else {
        mt->entries[idx].val = NULL;
        mt->entries[idx].vlen = 0;
    }
    mt->len++;
}

int memtable_get(const memtable_t *mt, const char *key, const char **val, size_t *vlen, int *is_tombstone) {
    size_t idx = lower_bound(mt, key);
    if (idx >= mt->len || strcmp(mt->entries[idx].key, key) != 0)
        return 0;

    *is_tombstone = mt->entries[idx].val == NULL;
    *val = mt->entries[idx].val;
    *vlen = mt->entries[idx].vlen;
    return 1;
}
