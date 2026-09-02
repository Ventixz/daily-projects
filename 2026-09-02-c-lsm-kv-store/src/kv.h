#ifndef KV_H
#define KV_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* ---- Memtable: sorted array of live entries, kept in key order ---- */

typedef struct {
    char *key;
    char *val;   /* NULL means tombstone (deleted) */
    size_t vlen; /* 0 for tombstones */
} kv_entry_t;

typedef struct {
    kv_entry_t *entries;
    size_t len;
    size_t cap;
} memtable_t;

void memtable_init(memtable_t *mt);
void memtable_free(memtable_t *mt);
/* Insert/overwrite. val == NULL records a tombstone. Copies key/val. */
void memtable_put(memtable_t *mt, const char *key, const char *val, size_t vlen);
/* Returns 1 and fills val/vlen/is_tombstone if key is present, else 0. */
int memtable_get(const memtable_t *mt, const char *key, const char **val, size_t *vlen, int *is_tombstone);

/* ---- Write-ahead log: durability for the memtable ---- */

typedef struct {
    char path[512];
    FILE *fp;
} wal_t;

int wal_open(wal_t *w, const char *path); /* opens for append, creating if needed */
void wal_close(wal_t *w);
int wal_append_put(wal_t *w, const char *key, const char *val, size_t vlen);
int wal_append_del(wal_t *w, const char *key);
/* Replays every record in path into mt. Returns 0 on success (including missing file). */
int wal_replay(const char *path, memtable_t *mt);
int wal_truncate(const char *path);

/* ---- SSTable: an immutable sorted run flushed from a memtable ---- */

typedef struct {
    char *key;
    uint64_t offset; /* byte offset of the record in the sstable file */
} sst_index_entry_t;

typedef struct {
    char path[512];
    uint32_t id;
    sst_index_entry_t *index; /* one entry per record, sorted by key */
    size_t count;
} sstable_t;

/* Writes mt's live entries (including tombstones) to a new sstable file and
 * loads its in-memory index. mt must already be sorted (it always is). */
int sstable_flush(const memtable_t *mt, const char *dir, uint32_t id, sstable_t *out);
/* Opens an existing sstable file and rebuilds its in-memory index. */
int sstable_open(const char *path, uint32_t id, sstable_t *out);
void sstable_free(sstable_t *s);
/* Binary search over the index, then a single seek+read. */
int sstable_get(const sstable_t *s, const char *key, char **val, size_t *vlen, int *is_tombstone);

/* Merge n sstables (newest last) into one new sstable at dir/id, keeping only
 * the newest version of each key and dropping tombstones. Returns 0 on success. */
int sstable_compact(sstable_t **inputs, size_t n, const char *dir, uint32_t id, sstable_t *out);

/* ---- Store: ties memtable + WAL + on-disk sstables together ---- */

#define KV_MEMTABLE_FLUSH_THRESHOLD 4
#define KV_COMPACT_THRESHOLD 3

typedef struct {
    char dir[512];
    memtable_t mt;
    wal_t wal;
    sstable_t *sstables; /* sstables[0] is oldest, sstables[n-1] is newest */
    size_t sstable_count;
    uint32_t next_sstable_id;
} kv_store_t;

int kv_open(kv_store_t *db, const char *dir);
void kv_close(kv_store_t *db);
int kv_put(kv_store_t *db, const char *key, const char *val, size_t vlen);
int kv_del(kv_store_t *db, const char *key);
/* Returns 1 and fills val/vlen if found and live, 0 if absent or deleted.
 * Caller must free the returned val. */
int kv_get(kv_store_t *db, const char *key, char **val, size_t *vlen);
int kv_flush(kv_store_t *db);   /* force memtable -> sstable, even below threshold */
int kv_compact(kv_store_t *db); /* force merge of all sstables into one */

#endif
