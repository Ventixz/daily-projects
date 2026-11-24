// Sorted set: every member is indexed twice.
//   - by name in a hash table, for O(1) ZSCORE / membership checks;
//   - by (score, name) in an AVL tree, for ordered and ranked queries.
// The node inherits both intrusive hooks, so one allocation serves both
// indexes and static_cast converts between hook and node.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "avl.h"
#include "hashtable.h"

struct ZNode : AVLNode, HNode {
    double score = 0;
    std::string name;
};

class ZSet {
public:
    ZSet() = default;
    ZSet(const ZSet &) = delete;
    ZSet &operator=(const ZSet &) = delete;
    ~ZSet() { clear(); }

    // Adds the member or updates its score. Returns true if it was new.
    bool insert(std::string_view name, double score);
    ZNode *lookup(std::string_view name);
    void remove(ZNode *node);
    // First member with (score, name) >= the given pair, or nullptr.
    ZNode *seek_ge(double score, std::string_view name);
    ZNode *at(int64_t rank);  // 0-based
    size_t size() const { return hm_size(&hmap_); }
    void clear();

    static ZNode *offset(ZNode *node, int64_t off);
    static int64_t rank(const ZNode *node) { return avl_rank(node); }

private:
    void tree_insert(ZNode *node);
    void update_score(ZNode *node, double score);

    AVLNode *root_ = nullptr;
    HMap hmap_;
};
