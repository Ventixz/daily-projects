// Sorted set: every member lives in two indexes at once.
//   - the hash map finds a member by name in O(1) (ZSCORE, ZREM, re-ZADD)
//   - the AVL tree keeps members ordered by (score, name) for range queries
// A ZNode inherits from both intrusive node types, so one allocation sits in
// both structures and static_cast gets from either index back to the member.
#pragma once

#include <string>
#include <string_view>

#include "avl.h"
#include "hashtable.h"

struct ZNode : AVLNode, HNode {
    double score;
    std::string name;
    ZNode(std::string_view n, double s);
};

class ZSet {
public:
    ZSet() = default;
    ~ZSet() { clear(); }
    ZSet(const ZSet &) = delete;
    ZSet &operator=(const ZSet &) = delete;

    // Adds the member, or moves it if the score changed. True if it was new.
    bool insert(std::string_view name, double score);
    ZNode *lookup(std::string_view name);
    bool remove(std::string_view name);

    // First member >= (score, name), or null.
    ZNode *seek_ge(double score, std::string_view name) const;
    // Member `off` places after `node` (negative = before), or null.
    static ZNode *offset(ZNode *node, int64_t off);
    static int64_t rank(const ZNode *node) { return avl_rank(node); }

    size_t size() const { return hmap_.size(); }
    const AVLNode *root() const { return root_; }
    void clear();

private:
    AVLNode *root_ = nullptr;
    HMap hmap_;

    void tree_insert(ZNode *node);
};
