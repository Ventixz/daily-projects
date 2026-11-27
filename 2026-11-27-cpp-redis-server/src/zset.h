// Sorted set: members ordered by (score, name), each name unique.
//
// Two indexes point at the same nodes: a hashtable keyed by name (for ZSCORE,
// ZREM and "does this member exist") and an AVL tree ordered by (score, name)
// (for range queries and ranks). The node inherits from both intrusive bases.
#pragma once
#include <cstddef>
#include <string_view>

#include "avl.h"
#include "hashtable.h"

struct ZNode : AVLNode, HNode {
    double score = 0;
    size_t len = 0;
    char *name = nullptr;  // owned, not NUL-terminated
};

struct ZSet {
    AVLNode *root = nullptr;
    HMap hmap;
};

// Adds `name` or moves it to a new score. Returns true if it was added.
bool zset_insert(ZSet *zset, std::string_view name, double score);
ZNode *zset_lookup(ZSet *zset, std::string_view name);
bool zset_delete(ZSet *zset, std::string_view name);
// First member >= (score, name), or null.
ZNode *zset_seekge(ZSet *zset, double score, std::string_view name);
ZNode *znode_offset(ZNode *node, int64_t offset);
int64_t znode_rank(const ZNode *node);
size_t zset_size(const ZSet *zset);
void zset_clear(ZSet *zset);  // frees every member

inline std::string_view znode_name(const ZNode *node) { return {node->name, node->len}; }
