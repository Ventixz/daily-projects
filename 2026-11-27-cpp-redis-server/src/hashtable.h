// Intrusive chained hashtable with progressive (incremental) rehashing.
//
// The table never stops the world to resize. When the load factor gets too
// high, a table twice the size is allocated. After that, every lookup, insert
// and delete moves a fixed number of nodes from the old table to the new one,
// so no single operation does more than O(k_rehashing_work) extra work.
//
// "Intrusive" means the table does not own or allocate nodes: callers embed an
// HNode in their own struct (here: by inheriting from it) and cast back.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>

struct HNode {
    HNode *next = nullptr;
    uint64_t hcode = 0;
};

struct HTab {
    HNode **tab = nullptr;  // array of bucket heads
    size_t mask = 0;        // capacity - 1 (capacity is a power of two)
    size_t size = 0;        // number of nodes stored
};

struct HMap {
    HTab newer;
    HTab older;              // non-empty only while a rehash is in progress
    size_t migrate_pos = 0;  // next bucket of `older` to move
};

// Compares a stored node with a lookup key. The key is whatever HNode-derived
// probe the caller passed in, so the callback knows its real type.
using HEq = bool (*)(HNode *stored, HNode *key);

HNode *hm_lookup(HMap *map, HNode *key, HEq eq);
void hm_insert(HMap *map, HNode *node);
HNode *hm_delete(HMap *map, HNode *key, HEq eq);  // detaches, does not free
void hm_clear(HMap *map);                         // frees bucket arrays only
size_t hm_size(const HMap *map);
bool hm_rehashing(const HMap *map);
// Visits every node. Return false from `f` to stop early. `f` may free the
// node it is given (e.g. when tearing the table down), but must not insert
// or delete through the map.
void hm_foreach(HMap *map, const std::function<bool(HNode *)> &f);

uint64_t str_hash(const void *data, size_t len);

constexpr size_t k_max_load_factor = 8;
constexpr size_t k_rehashing_work = 128;
