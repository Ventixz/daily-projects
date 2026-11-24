#include "hashtable.h"

#include <cassert>
#include <cstdlib>

namespace {

constexpr size_t kRehashWork = 128;  // buckets visited per operation
constexpr size_t kMaxLoadFactor = 8;

void h_init(HTab *htab, size_t n) {
    assert(n > 0 && ((n - 1) & n) == 0);
    htab->tab = static_cast<HNode **>(calloc(n, sizeof(HNode *)));
    if (!htab->tab) abort();
    htab->mask = n - 1;
    htab->size = 0;
}

void h_insert(HTab *htab, HNode *node) {
    size_t pos = node->hcode & htab->mask;
    node->next = htab->tab[pos];
    htab->tab[pos] = node;
    htab->size++;
}

// Returns the address of the pointer that points at the match, so the caller
// can unlink it without a separate "previous node" variable.
HNode **h_lookup(HTab *htab, const HNode *key, HEqFn eq) {
    if (!htab->tab) return nullptr;
    HNode **from = &htab->tab[key->hcode & htab->mask];
    for (HNode *cur; (cur = *from) != nullptr; from = &cur->next) {
        if (cur->hcode == key->hcode && eq(cur, key)) return from;
    }
    return nullptr;
}

HNode *h_detach(HTab *htab, HNode **from) {
    HNode *node = *from;
    *from = node->next;
    htab->size--;
    return node;
}

void hm_help_rehashing(HMap *hmap) {
    size_t nwork = 0;
    while (nwork < kRehashWork && hmap->older.size > 0) {
        HNode **from = &hmap->older.tab[hmap->migrate_pos];
        if (!*from) {
            hmap->migrate_pos++;
        } else {
            h_insert(&hmap->newer, h_detach(&hmap->older, from));
        }
        nwork++;  // empty buckets count too, so the work per call is bounded
    }
    if (hmap->older.size == 0 && hmap->older.tab) {
        free(hmap->older.tab);
        hmap->older = HTab{};
    }
}

void hm_trigger_rehashing(HMap *hmap) {
    assert(hmap->older.tab == nullptr);
    hmap->older = hmap->newer;
    h_init(&hmap->newer, (hmap->newer.mask + 1) * 2);
    hmap->migrate_pos = 0;
}

bool h_foreach(HTab *htab, bool (*f)(HNode *, void *), void *arg) {
    for (size_t i = 0; htab->tab && i <= htab->mask; i++) {
        for (HNode *node = htab->tab[i], *next; node; node = next) {
            next = node->next;  // read first: the callback may free `node`
            if (!f(node, arg)) return false;
        }
    }
    return true;
}

}  // namespace

HNode *hm_lookup(HMap *hmap, const HNode *key, HEqFn eq) {
    hm_help_rehashing(hmap);
    HNode **from = h_lookup(&hmap->newer, key, eq);
    if (!from) from = h_lookup(&hmap->older, key, eq);
    return from ? *from : nullptr;
}

void hm_insert(HMap *hmap, HNode *node) {
    if (!hmap->newer.tab) h_init(&hmap->newer, 4);
    h_insert(&hmap->newer, node);
    if (!hmap->older.tab) {
        size_t threshold = (hmap->newer.mask + 1) * kMaxLoadFactor;
        if (hmap->newer.size >= threshold) hm_trigger_rehashing(hmap);
    }
    hm_help_rehashing(hmap);
}

HNode *hm_delete(HMap *hmap, const HNode *key, HEqFn eq) {
    hm_help_rehashing(hmap);
    if (HNode **from = h_lookup(&hmap->newer, key, eq)) return h_detach(&hmap->newer, from);
    if (HNode **from = h_lookup(&hmap->older, key, eq)) return h_detach(&hmap->older, from);
    return nullptr;
}

void hm_clear(HMap *hmap) {
    free(hmap->newer.tab);
    free(hmap->older.tab);
    *hmap = HMap{};
}

size_t hm_size(const HMap *hmap) { return hmap->newer.size + hmap->older.size; }

void hm_foreach(HMap *hmap, bool (*f)(HNode *, void *), void *arg) {
    h_foreach(&hmap->newer, f, arg) && h_foreach(&hmap->older, f, arg);
}

bool hm_rehashing(const HMap *hmap) { return hmap->older.tab != nullptr; }

uint64_t str_hash(const uint8_t *data, size_t len) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < len; i++) {
        h ^= data[i];
        h *= 0x100000001b3ull;
    }
    return h;
}
