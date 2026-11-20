#include "hashtable.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

enum { K_MAX_LOAD_FACTOR = 8, K_REHASHING_WORK = 128, K_INITIAL_SLOTS = 4 };

static void h_init(HTab *t, size_t n) {
    assert(n > 0 && (n & (n - 1)) == 0);
    t->tab = calloc(n, sizeof(HNode *));
    if (!t->tab) { perror("calloc"); abort(); }
    t->mask = n - 1;
    t->size = 0;
}

static void h_insert(HTab *t, HNode *node) {
    size_t pos = node->hcode & t->mask;
    node->next = t->tab[pos];
    t->tab[pos] = node;
    t->size++;
}

/* Returns the address of the pointer that points at the match, so the caller
 * can unlink it without a separate "previous" pointer. */
static HNode **h_lookup(HTab *t, HNode *key, HEqFn eq) {
    if (!t->tab) return NULL;
    HNode **from = &t->tab[key->hcode & t->mask];
    for (HNode *cur; (cur = *from) != NULL; from = &cur->next) {
        if (cur->hcode == key->hcode && eq(cur, key)) return from;
    }
    return NULL;
}

static HNode *h_detach(HTab *t, HNode **from) {
    HNode *node = *from;
    *from = node->next;
    t->size--;
    return node;
}

static void hm_help_rehashing(HMap *hmap) {
    size_t nwork = 0;
    while (nwork < K_REHASHING_WORK && hmap->older.size > 0) {
        HNode **from = &hmap->older.tab[hmap->migrate_pos];
        if (!*from) {
            hmap->migrate_pos++;
            continue;
        }
        h_insert(&hmap->newer, h_detach(&hmap->older, from));
        nwork++;
    }
    if (hmap->older.size == 0 && hmap->older.tab) {
        free(hmap->older.tab);
        hmap->older = (HTab){0};
    }
}

static void hm_trigger_rehashing(HMap *hmap) {
    assert(hmap->older.tab == NULL);
    hmap->older = hmap->newer;
    h_init(&hmap->newer, (hmap->newer.mask + 1) * 2);
    hmap->migrate_pos = 0;
}

HNode *hm_lookup(HMap *hmap, HNode *key, HEqFn eq) {
    hm_help_rehashing(hmap);
    HNode **from = h_lookup(&hmap->newer, key, eq);
    if (!from) from = h_lookup(&hmap->older, key, eq);
    return from ? *from : NULL;
}

void hm_insert(HMap *hmap, HNode *node) {
    if (!hmap->newer.tab) h_init(&hmap->newer, K_INITIAL_SLOTS);
    h_insert(&hmap->newer, node);
    if (!hmap->older.tab) {
        size_t threshold = (hmap->newer.mask + 1) * K_MAX_LOAD_FACTOR;
        if (hmap->newer.size >= threshold) hm_trigger_rehashing(hmap);
    }
    hm_help_rehashing(hmap);
}

HNode *hm_delete(HMap *hmap, HNode *key, HEqFn eq) {
    hm_help_rehashing(hmap);
    HNode **from = h_lookup(&hmap->newer, key, eq);
    if (from) return h_detach(&hmap->newer, from);
    from = h_lookup(&hmap->older, key, eq);
    if (from) return h_detach(&hmap->older, from);
    return NULL;
}

void hm_clear(HMap *hmap) {
    free(hmap->newer.tab);
    free(hmap->older.tab);
    *hmap = (HMap){0};
}

size_t hm_size(const HMap *hmap) { return hmap->newer.size + hmap->older.size; }

bool hm_rehashing(const HMap *hmap) { return hmap->older.tab != NULL; }

static bool h_foreach(HTab *t, bool (*f)(HNode *, void *), void *arg) {
    for (size_t i = 0; t->tab && i <= t->mask; i++) {
        for (HNode *node = t->tab[i]; node; node = node->next) {
            if (!f(node, arg)) return false;
        }
    }
    return true;
}

void hm_foreach(HMap *hmap, bool (*f)(HNode *, void *), void *arg) {
    if (h_foreach(&hmap->newer, f, arg)) h_foreach(&hmap->older, f, arg);
}
