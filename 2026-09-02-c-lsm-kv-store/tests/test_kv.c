#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/kv.h"

static void rmrf(const char *dir) {
    char cmd[600];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", dir);
    system(cmd); /* test-only cleanup; never runs on user input */
}

/* ---- memtable ---- */

static void test_memtable_put_get_overwrite(void) {
    memtable_t mt;
    memtable_init(&mt);

    memtable_put(&mt, "b", "2", 1);
    memtable_put(&mt, "a", "1", 1);
    memtable_put(&mt, "c", "3", 1);
    assert(mt.len == 3);
    /* stays sorted by key */
    assert(strcmp(mt.entries[0].key, "a") == 0);
    assert(strcmp(mt.entries[1].key, "b") == 0);
    assert(strcmp(mt.entries[2].key, "c") == 0);

    const char *val;
    size_t vlen;
    int tomb;
    assert(memtable_get(&mt, "b", &val, &vlen, &tomb) == 1);
    assert(!tomb && vlen == 1 && val[0] == '2');

    memtable_put(&mt, "b", "22", 2); /* overwrite, no new entry */
    assert(mt.len == 3);
    assert(memtable_get(&mt, "b", &val, &vlen, &tomb) == 1);
    assert(vlen == 2 && memcmp(val, "22", 2) == 0);

    assert(memtable_get(&mt, "missing", &val, &vlen, &tomb) == 0);

    memtable_free(&mt);
    printf("PASS test_memtable_put_get_overwrite\n");
}

static void test_memtable_tombstone(void) {
    memtable_t mt;
    memtable_init(&mt);

    memtable_put(&mt, "k", "v", 1);
    memtable_put(&mt, "k", NULL, 0); /* delete */
    assert(mt.len == 1);             /* tombstone replaces, doesn't add */

    const char *val;
    size_t vlen;
    int tomb;
    assert(memtable_get(&mt, "k", &val, &vlen, &tomb) == 1);
    assert(tomb == 1);

    memtable_free(&mt);
    printf("PASS test_memtable_tombstone\n");
}

/* ---- kv_store: basic put/get/del below the flush threshold ---- */

static void test_store_put_get_del_in_memtable(void) {
    const char *dir = "test_kvdata_basic";
    rmrf(dir);

    kv_store_t db;
    assert(kv_open(&db, dir) == 0);

    assert(kv_put(&db, "name", "claude", 6) == 0);
    assert(kv_put(&db, "lang", "c", 1) == 0);
    assert(db.mt.len == 2 && db.sstable_count == 0); /* below threshold, no flush */

    char *val;
    size_t vlen;
    assert(kv_get(&db, "name", &val, &vlen) == 1);
    assert(vlen == 6 && memcmp(val, "claude", 6) == 0);
    free(val);

    assert(kv_del(&db, "lang") == 0);
    assert(kv_get(&db, "lang", &val, &vlen) == 0);
    assert(kv_get(&db, "nope", &val, &vlen) == 0);

    kv_close(&db);
    rmrf(dir);
    printf("PASS test_store_put_get_del_in_memtable\n");
}

/* ---- flush: crossing KV_MEMTABLE_FLUSH_THRESHOLD moves data to an sstable ---- */

static void test_store_flush_moves_data_to_sstable(void) {
    const char *dir = "test_kvdata_flush";
    rmrf(dir);

    kv_store_t db;
    assert(kv_open(&db, dir) == 0);

    for (int i = 0; i < KV_MEMTABLE_FLUSH_THRESHOLD; i++) {
        char key[16], val[16];
        snprintf(key, sizeof(key), "k%d", i);
        snprintf(val, sizeof(val), "v%d", i);
        assert(kv_put(&db, key, val, strlen(val)) == 0);
    }
    /* the threshold-th put triggers an automatic flush */
    assert(db.mt.len == 0);
    assert(db.sstable_count == 1);
    assert(db.sstables[0].count == (size_t)KV_MEMTABLE_FLUSH_THRESHOLD);

    /* every key is still reachable, now served from the sstable */
    for (int i = 0; i < KV_MEMTABLE_FLUSH_THRESHOLD; i++) {
        char key[16], expect[16];
        snprintf(key, sizeof(key), "k%d", i);
        snprintf(expect, sizeof(expect), "v%d", i);
        char *val;
        size_t vlen;
        assert(kv_get(&db, key, &val, &vlen) == 1);
        assert(vlen == strlen(expect) && memcmp(val, expect, vlen) == 0);
        free(val);
    }

    kv_close(&db);
    rmrf(dir);
    printf("PASS test_store_flush_moves_data_to_sstable\n");
}

/* ---- newer sstables (and the memtable) shadow older ones for the same key ---- */

static void test_store_newer_overrides_older_across_sstables(void) {
    const char *dir = "test_kvdata_shadow";
    rmrf(dir);

    kv_store_t db;
    assert(kv_open(&db, dir) == 0);

    assert(kv_put(&db, "x", "v1", 2) == 0);
    assert(kv_flush(&db) == 0); /* sstable 0: x=v1 */

    assert(kv_put(&db, "x", "v2", 2) == 0);
    assert(kv_flush(&db) == 0); /* sstable 1: x=v2, newer */

    assert(kv_put(&db, "x", "v3", 2) == 0); /* still in the memtable, newest */

    char *val;
    size_t vlen;
    assert(kv_get(&db, "x", &val, &vlen) == 1);
    assert(vlen == 2 && memcmp(val, "v3", 2) == 0);
    free(val);

    assert(kv_flush(&db) == 0);
    /* two flushes plus this one push sstable_count to 3, which is
     * KV_COMPACT_THRESHOLD -- compaction should have collapsed them to 1. */
    assert(db.sstable_count == 1);
    assert(kv_get(&db, "x", &val, &vlen) == 1);
    assert(vlen == 2 && memcmp(val, "v3", 2) == 0);
    free(val);

    kv_close(&db);
    rmrf(dir);
    printf("PASS test_store_newer_overrides_older_across_sstables\n");
}

/* ---- compaction drops tombstones once nothing older is left to shadow ---- */

static void test_compact_drops_tombstones(void) {
    const char *dir = "test_kvdata_compact";
    rmrf(dir);

    kv_store_t db;
    assert(kv_open(&db, dir) == 0);

    assert(kv_put(&db, "a", "1", 1) == 0);
    assert(kv_put(&db, "b", "2", 1) == 0);
    assert(kv_flush(&db) == 0); /* sstable: a=1, b=2 */

    assert(kv_del(&db, "a") == 0);
    assert(kv_flush(&db) == 0); /* sstable: a=<tombstone> */

    assert(db.sstable_count == 2);
    assert(kv_compact(&db) == 0);
    assert(db.sstable_count == 1);
    assert(db.sstables[0].count == 1); /* only "b" survives; "a" and its tombstone are gone */

    char *val;
    size_t vlen;
    assert(kv_get(&db, "a", &val, &vlen) == 0);
    assert(kv_get(&db, "b", &val, &vlen) == 1);
    assert(vlen == 1 && val[0] == '2');
    free(val);

    kv_close(&db);
    rmrf(dir);
    printf("PASS test_compact_drops_tombstones\n");
}

/* ---- WAL replay recovers memtable writes that were never flushed ---- */

static void test_wal_replay_recovers_unflushed_writes(void) {
    const char *dir = "test_kvdata_crash";
    rmrf(dir);

    kv_store_t db;
    assert(kv_open(&db, dir) == 0);
    assert(kv_put(&db, "durable", "yes", 3) == 0);
    assert(kv_put(&db, "gone", "no", 2) == 0);
    assert(kv_del(&db, "gone") == 0);
    assert(db.mt.len == 2 && db.sstable_count == 0); /* nothing flushed to disk yet */
    /* simulate a crash: close the WAL handle without an orderly flush */
    kv_close(&db);

    kv_store_t db2;
    assert(kv_open(&db2, dir) == 0);
    assert(db2.sstable_count == 0);
    assert(db2.mt.len == 2); /* WAL replay rebuilt the memtable */

    char *val;
    size_t vlen;
    assert(kv_get(&db2, "durable", &val, &vlen) == 1);
    assert(vlen == 3 && memcmp(val, "yes", 3) == 0);
    free(val);
    assert(kv_get(&db2, "gone", &val, &vlen) == 0);

    kv_close(&db2);
    rmrf(dir);
    printf("PASS test_wal_replay_recovers_unflushed_writes\n");
}

/* ---- restart after a flush: sstables reload from disk, WAL stays empty ---- */

static void test_restart_reloads_sstables_from_disk(void) {
    const char *dir = "test_kvdata_restart";
    rmrf(dir);

    kv_store_t db;
    assert(kv_open(&db, dir) == 0);
    assert(kv_put(&db, "p", "q", 1) == 0);
    assert(kv_flush(&db) == 0);
    kv_close(&db);

    kv_store_t db2;
    assert(kv_open(&db2, dir) == 0);
    assert(db2.sstable_count == 1);
    assert(db2.mt.len == 0); /* WAL was truncated by the flush, nothing to replay */

    char *val;
    size_t vlen;
    assert(kv_get(&db2, "p", &val, &vlen) == 1);
    assert(vlen == 1 && val[0] == 'q');
    free(val);

    kv_close(&db2);
    rmrf(dir);
    printf("PASS test_restart_reloads_sstables_from_disk\n");
}

int main(void) {
    test_memtable_put_get_overwrite();
    test_memtable_tombstone();
    test_store_put_get_del_in_memtable();
    test_store_flush_moves_data_to_sstable();
    test_store_newer_overrides_older_across_sstables();
    test_compact_drops_tombstones();
    test_wal_replay_recovers_unflushed_writes();
    test_restart_reloads_sstables_from_disk();
    printf("\nAll tests passed.\n");
    return 0;
}
