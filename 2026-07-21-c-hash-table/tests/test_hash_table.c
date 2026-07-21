#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../src/hash_table.h"

static void test_insert_and_search(void) {
    ht_hash_table* ht = ht_new();
    ht_insert(ht, "name", "claude");
    ht_insert(ht, "lang", "c");

    assert(strcmp(ht_search(ht, "name"), "claude") == 0);
    assert(strcmp(ht_search(ht, "lang"), "c") == 0);
    assert(ht_search(ht, "missing") == NULL);

    ht_del_hash_table(ht);
    printf("PASS test_insert_and_search\n");
}

static void test_overwrite_existing_key(void) {
    ht_hash_table* ht = ht_new();
    ht_insert(ht, "key", "first");
    ht_insert(ht, "key", "second");

    assert(strcmp(ht_search(ht, "key"), "second") == 0);
    assert(ht->count == 1);

    ht_del_hash_table(ht);
    printf("PASS test_overwrite_existing_key\n");
}

static void test_delete(void) {
    ht_hash_table* ht = ht_new();
    ht_insert(ht, "a", "1");
    ht_insert(ht, "b", "2");

    ht_delete(ht, "a");
    assert(ht_search(ht, "a") == NULL);
    assert(strcmp(ht_search(ht, "b"), "2") == 0);

    ht_del_hash_table(ht);
    printf("PASS test_delete\n");
}

/* Insert enough items to force ht_resize_up, then confirm every key
 * still resolves correctly through the new, larger bucket array. */
static void test_resize_preserves_items(void) {
    ht_hash_table* ht = ht_new();
    char key[16], value[16];
    const int n = 100;

    for (int i = 0; i < n; i++) {
        snprintf(key, sizeof(key), "key%d", i);
        snprintf(value, sizeof(value), "val%d", i);
        ht_insert(ht, key, value);
    }
    assert(ht->size > 53); /* confirms a resize actually happened */

    for (int i = 0; i < n; i++) {
        snprintf(key, sizeof(key), "key%d", i);
        snprintf(value, sizeof(value), "val%d", i);
        char* got = ht_search(ht, key);
        assert(got != NULL);
        assert(strcmp(got, value) == 0);
    }

    ht_del_hash_table(ht);
    printf("PASS test_resize_preserves_items\n");
}

/* Deleting most items should shrink the table back down (ht_resize_down)
 * without losing whatever's left. */
static void test_resize_down_after_deletes(void) {
    ht_hash_table* ht = ht_new();
    char key[16];
    const int n = 100;

    for (int i = 0; i < n; i++) {
        snprintf(key, sizeof(key), "key%d", i);
        ht_insert(ht, key, "x");
    }
    int grown_size = ht->size;

    for (int i = 0; i < n - 2; i++) {
        snprintf(key, sizeof(key), "key%d", i);
        ht_delete(ht, key);
    }
    assert(ht->size < grown_size);
    assert(strcmp(ht_search(ht, "key98"), "x") == 0);
    assert(strcmp(ht_search(ht, "key99"), "x") == 0);

    ht_del_hash_table(ht);
    printf("PASS test_resize_down_after_deletes\n");
}

int main(void) {
    test_insert_and_search();
    test_overwrite_existing_key();
    test_delete();
    test_resize_preserves_items();
    test_resize_down_after_deletes();
    printf("All tests passed.\n");
    return 0;
}
