#include "hashtable.h"

#include <cassert>
#include <cstdlib>

static void h_init(HTab *t, size_t n) {
    assert(n > 0 && (n & (n - 1)) == 0);
    t->tab = static_cast<HNode **>(calloc(n, sizeof(HNode *)));
    if (!t->tab) abort();
    t->mask = n - 1;
    t->size = 0;
}

static void h_insert(HTab *t, HNode *node) {
    size_t pos = node->hcode & t->mask;
    node->next = t->tab[pos];
    t->tab[pos] = node;
    t->size++;
}

// Returns the address of the pointer that points at the match, so the caller
// can unlink it without a "previous" pointer.
static HNode **h_lookup(HTab *t, HNode *key, HEq eq) {
    if (!t->tab) return nullptr;
    HNode **from = &t->tab[key->hcode & t->mask];
    for (HNode *cur; (cur = *from) != nullptr; from = &cur->next) {
        if (cur->hcode == key->hcode && eq(cur, key)) return from;
    }
    return nullptr;
}

static HNode *h_detach(HTab *t, HNode **from) {
    HNode *node = *from;
    *from = node->next;
    node->next = nullptr;
    t->size--;
    return node;
}

static void hm_help_rehashing(HMap *m) {
    size_t nwork = 0;
    while (nwork < k_rehashing_work && m->older.size > 0) {
        // Nodes left in `older` always sit at or after migrate_pos.
        HNode **from = &m->older.tab[m->migrate_pos];
        if (!*from) {
            m->migrate_pos++;
            continue;
        }
        h_insert(&m->newer, h_detach(&m->older, from));
        nwork++;
    }
    if (m->older.size == 0 && m->older.tab) {
        free(m->older.tab);
        m->older = HTab{};
    }
}

static void hm_trigger_rehashing(HMap *m) {
    assert(!m->older.tab);
    m->older = m->newer;
    h_init(&m->newer, (m->newer.mask + 1) * 2);
    m->migrate_pos = 0;
}

HNode *hm_lookup(HMap *m, HNode *key, HEq eq) {
    hm_help_rehashing(m);
    HNode **from = h_lookup(&m->newer, key, eq);
    if (!from) from = h_lookup(&m->older, key, eq);
    return from ? *from : nullptr;
}

void hm_insert(HMap *m, HNode *node) {
    if (!m->newer.tab) h_init(&m->newer, 4);
    h_insert(&m->newer, node);
    if (!m->older.tab) {
        size_t threshold = (m->newer.mask + 1) * k_max_load_factor;
        if (m->newer.size >= threshold) hm_trigger_rehashing(m);
    }
    hm_help_rehashing(m);
}

HNode *hm_delete(HMap *m, HNode *key, HEq eq) {
    hm_help_rehashing(m);
    if (HNode **from = h_lookup(&m->newer, key, eq)) return h_detach(&m->newer, from);
    if (HNode **from = h_lookup(&m->older, key, eq)) return h_detach(&m->older, from);
    return nullptr;
}

void hm_clear(HMap *m) {
    free(m->newer.tab);
    free(m->older.tab);
    *m = HMap{};
}

size_t hm_size(const HMap *m) { return m->newer.size + m->older.size; }

bool hm_rehashing(const HMap *m) { return m->older.tab != nullptr; }

static bool h_foreach(HTab *t, const std::function<bool(HNode *)> &f) {
    for (size_t i = 0; t->tab && i <= t->mask; i++) {
        for (HNode *node = t->tab[i], *next; node; node = next) {
            next = node->next;  // read first, so `f` may free the node
            if (!f(node)) return false;
        }
    }
    return true;
}

void hm_foreach(HMap *m, const std::function<bool(HNode *)> &f) {
    if (h_foreach(&m->newer, f)) h_foreach(&m->older, f);
}

// FNV-1a, 64-bit.
uint64_t str_hash(const void *data, size_t len) {
    const uint8_t *p = static_cast<const uint8_t *>(data);
    uint64_t h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}
