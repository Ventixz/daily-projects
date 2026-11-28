#include "hashtable.h"

#include <cassert>
#include <cstdlib>
#include <new>

namespace {

constexpr size_t k_max_load_factor = 8;  // average chain length that triggers a resize
constexpr size_t k_rehashing_work = 128;  // nodes moved per map operation

void h_init(HTab *t, size_t n) {
    assert(n > 0 && (n & (n - 1)) == 0);
    t->tab = static_cast<HNode **>(std::calloc(n, sizeof(HNode *)));
    if (!t->tab) throw std::bad_alloc();
    t->mask = n - 1;
    t->size = 0;
}

void h_insert(HTab *t, HNode *node) {
    size_t pos = node->hcode & t->mask;
    node->next = t->tab[pos];
    t->tab[pos] = node;
    t->size++;
}

// Returns the address of the pointer that points at the match, so the caller
// can unlink it without knowing whether it is a slot head or a chain member.
HNode **h_lookup(HTab *t, const HNode *key, HMap::Eq eq) {
    if (!t->tab) return nullptr;
    HNode **from = &t->tab[key->hcode & t->mask];
    for (HNode *cur; (cur = *from) != nullptr; from = &cur->next) {
        if (cur->hcode == key->hcode && eq(cur, key)) return from;
    }
    return nullptr;
}

HNode *h_detach(HTab *t, HNode **from) {
    HNode *node = *from;
    *from = node->next;
    t->size--;
    return node;
}

}  // namespace

uint64_t hash_bytes(const void *data, size_t len) {
    const auto *p = static_cast<const uint8_t *>(data);
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

HMap::~HMap() { clear(); }

void HMap::clear() {
    std::free(newer_.tab);
    std::free(older_.tab);
    newer_ = HTab{};
    older_ = HTab{};
    migrate_pos_ = 0;
}

void HMap::help_rehashing() {
    size_t nwork = 0;
    while (nwork < k_rehashing_work && older_.size > 0) {
        assert(migrate_pos_ <= older_.mask);
        HNode **from = &older_.tab[migrate_pos_];
        if (!*from) {
            migrate_pos_++;
            continue;
        }
        h_insert(&newer_, h_detach(&older_, from));
        nwork++;
    }
    if (older_.size == 0 && older_.tab) {
        std::free(older_.tab);
        older_ = HTab{};
    }
}

void HMap::trigger_rehashing() {
    assert(older_.tab == nullptr);
    older_ = newer_;
    h_init(&newer_, (older_.mask + 1) * 2);
    migrate_pos_ = 0;
}

HNode *HMap::lookup(const HNode *key, Eq eq) {
    help_rehashing();
    HNode **from = h_lookup(&newer_, key, eq);
    if (!from) from = h_lookup(&older_, key, eq);
    return from ? *from : nullptr;
}

void HMap::insert(HNode *node) {
    if (!newer_.tab) h_init(&newer_, 4);
    h_insert(&newer_, node);
    if (!older_.tab && newer_.size >= (newer_.mask + 1) * k_max_load_factor) {
        trigger_rehashing();
    }
    help_rehashing();
}

HNode *HMap::remove(const HNode *key, Eq eq) {
    help_rehashing();
    if (HNode **from = h_lookup(&newer_, key, eq)) return h_detach(&newer_, from);
    if (HNode **from = h_lookup(&older_, key, eq)) return h_detach(&older_, from);
    return nullptr;
}
