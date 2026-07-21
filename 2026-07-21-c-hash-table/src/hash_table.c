#define _POSIX_C_SOURCE 200809L /* for strdup, not in plain C99 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hash_table.h"
#include "prime.h"

#define HT_INITIAL_BASE_SIZE 53
#define HT_PRIME_1 151
#define HT_PRIME_2 163

/* Sentinel used to mark a bucket that once held an item but was deleted.
 * It has to be distinct from NULL (empty bucket) so probing doesn't stop
 * early and lose items that were inserted after a since-deleted one. */
static ht_item HT_DELETED_ITEM = {NULL, NULL};

static ht_item* ht_new_item(const char* key, const char* value) {
    ht_item* i = malloc(sizeof(ht_item));
    i->key = strdup(key);
    i->value = strdup(value);
    return i;
}

static void ht_del_item(ht_item* i) {
    if (i == NULL || i == &HT_DELETED_ITEM) {
        return;
    }
    free(i->key);
    free(i->value);
    free(i);
}

static ht_hash_table* ht_new_sized(int base_size) {
    ht_hash_table* ht = malloc(sizeof(ht_hash_table));
    ht->base_size = base_size;
    ht->size = next_prime(ht->base_size);
    ht->count = 0;
    ht->items = calloc((size_t)ht->size, sizeof(ht_item*));
    return ht;
}

ht_hash_table* ht_new(void) {
    return ht_new_sized(HT_INITIAL_BASE_SIZE);
}

void ht_del_hash_table(ht_hash_table* ht) {
    for (int i = 0; i < ht->size; i++) {
        ht_item* item = ht->items[i];
        if (item != NULL) {
            ht_del_item(item);
        }
    }
    free(ht->items);
    free(ht);
}

/* Polynomial rolling hash of `s`, reduced mod `m`. `a` should be a prime
 * larger than the alphabet size (ASCII: 128) so distinct characters map to
 * distinct contributions. */
static int ht_hash(const char* s, int a, int m) {
    long hash = 0;
    size_t len = strlen(s);
    for (size_t i = 0; i < len; i++) {
        hash += (long)pow(a, (double)(len - (i + 1))) * s[i];
        hash %= m;
    }
    return (int)hash;
}

/* Open addressing via double hashing: on collision, probe
 * (hash_a + attempt * (hash_b + 1)) mod num_buckets. The "+1" on hash_b
 * guards against a zero step size, which would degenerate into linear
 * probing (or an infinite loop on a full table). */
static int ht_get_hash(const char* s, int num_buckets, int attempt) {
    int hash_a = ht_hash(s, HT_PRIME_1, num_buckets);
    int hash_b = ht_hash(s, HT_PRIME_2, num_buckets);
    return (hash_a + attempt * (hash_b + 1)) % num_buckets;
}

static void ht_resize(ht_hash_table* ht, int base_size) {
    if (base_size < HT_INITIAL_BASE_SIZE) {
        return;
    }
    ht_hash_table* new_ht = ht_new_sized(base_size);
    for (int i = 0; i < ht->size; i++) {
        ht_item* item = ht->items[i];
        if (item != NULL && item != &HT_DELETED_ITEM) {
            ht_insert(new_ht, item->key, item->value);
        }
    }

    /* Swap the bucket arrays/sizes so `ht` keeps its identity for callers
     * holding a pointer to it, then delete what's left of new_ht (which is
     * now the old, smaller table). */
    ht->base_size = new_ht->base_size;
    ht->count = new_ht->count;

    int tmp_size = ht->size;
    ht->size = new_ht->size;
    new_ht->size = tmp_size;

    ht_item** tmp_items = ht->items;
    ht->items = new_ht->items;
    new_ht->items = tmp_items;

    ht_del_hash_table(new_ht);
}

static void ht_resize_up(ht_hash_table* ht) {
    ht_resize(ht, ht->base_size * 2);
}

static void ht_resize_down(ht_hash_table* ht) {
    ht_resize(ht, ht->base_size / 2);
}

void ht_insert(ht_hash_table* ht, const char* key, const char* value) {
    int load = ht->count * 100 / ht->size;
    if (load > 70) {
        ht_resize_up(ht);
    }

    ht_item* item = ht_new_item(key, value);
    int index = ht_get_hash(item->key, ht->size, 0);
    ht_item* cur = ht->items[index];
    int i = 1;
    while (cur != NULL) {
        if (cur != &HT_DELETED_ITEM && strcmp(cur->key, key) == 0) {
            ht_del_item(cur);
            ht->items[index] = item;
            return;
        }
        index = ht_get_hash(item->key, ht->size, i);
        cur = ht->items[index];
        i++;
    }
    ht->items[index] = item;
    ht->count++;
}

char* ht_search(ht_hash_table* ht, const char* key) {
    int index = ht_get_hash(key, ht->size, 0);
    ht_item* item = ht->items[index];
    int i = 1;
    while (item != NULL) {
        if (item != &HT_DELETED_ITEM && strcmp(item->key, key) == 0) {
            return item->value;
        }
        index = ht_get_hash(key, ht->size, i);
        item = ht->items[index];
        i++;
    }
    return NULL;
}

void ht_delete(ht_hash_table* ht, const char* key) {
    int load = ht->count * 100 / ht->size;
    if (load < 10) {
        ht_resize_down(ht);
    }

    int index = ht_get_hash(key, ht->size, 0);
    ht_item* item = ht->items[index];
    int i = 1;
    while (item != NULL) {
        if (item != &HT_DELETED_ITEM && strcmp(item->key, key) == 0) {
            ht_del_item(item);
            ht->items[index] = &HT_DELETED_ITEM;
            ht->count--;
            return;
        }
        index = ht_get_hash(key, ht->size, i);
        item = ht->items[index];
        i++;
    }
}
