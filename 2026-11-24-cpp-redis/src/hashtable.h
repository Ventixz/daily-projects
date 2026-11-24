// Intrusive chained hash table with progressive (incremental) rehashing.
//
// "Intrusive" means the table never allocates nodes: callers embed an HNode in
// their own struct (here by inheriting from it) and the table only links those
// nodes together. Resizing is spread across operations so that no single
// request pays for moving every key, which keeps worst-case latency flat.
#pragma once

#include <cstddef>
#include <cstdint>

struct HNode {
    HNode *next = nullptr;
    uint64_t hcode = 0;  // cached hash of the key
};

// One fixed-size, power-of-two table.
struct HTab {
    HNode **tab = nullptr;
    size_t mask = 0;  // n_buckets - 1
    size_t size = 0;  // number of nodes stored
};

// The public map: two tables, so a resize can be done a little at a time.
// New inserts go to `newer`; `older` is drained by hm_help_rehashing().
struct HMap {
    HTab newer;
    HTab older;
    size_t migrate_pos = 0;
};

using HEqFn = bool (*)(const HNode *, const HNode *);

HNode *hm_lookup(HMap *hmap, const HNode *key, HEqFn eq);
void hm_insert(HMap *hmap, HNode *node);
HNode *hm_delete(HMap *hmap, const HNode *key, HEqFn eq);
void hm_clear(HMap *hmap);  // frees bucket arrays only, never the nodes
size_t hm_size(const HMap *hmap);
// Calls f for every node until it returns false. The callback may free the
// node it was given (used for teardown) but must not otherwise modify the map.
void hm_foreach(HMap *hmap, bool (*f)(HNode *, void *), void *arg);
// Exposed for tests: is a resize currently in progress?
bool hm_rehashing(const HMap *hmap);

// FNV-1a. Not DoS-resistant; good enough for a learning project.
uint64_t str_hash(const uint8_t *data, size_t len);
