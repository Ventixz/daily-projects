// Intrusive AVL tree augmented with subtree sizes.
//
// The tree only knows about links, heights and counts. Ordering lives in the
// caller (zset.cpp): it walks down to find the empty slot, links the node, and
// calls avl_fix to rebalance. Keeping `cnt` (the subtree size) in every node is
// what makes rank queries and "skip N items" O(log n) instead of O(n).
#pragma once

#include <cstdint>

struct AVLNode {
    AVLNode *parent = nullptr;
    AVLNode *left = nullptr;
    AVLNode *right = nullptr;
    uint32_t height = 1;  // levels in this subtree, a lone node is 1
    uint32_t cnt = 1;     // nodes in this subtree, including itself
};

inline uint32_t avl_height(const AVLNode *n) { return n ? n->height : 0; }
inline uint32_t avl_cnt(const AVLNode *n) { return n ? n->cnt : 0; }

// Rebalance from a freshly linked (or changed) node up to the root.
// Returns the new root.
AVLNode *avl_fix(AVLNode *node);

// Unlink `node` and rebalance. Returns the new root (null if the tree emptied).
AVLNode *avl_del(AVLNode *node);

// The node `offset` positions after `node` in sorted order (negative = before),
// or null if that runs off either end. O(log n).
AVLNode *avl_offset(AVLNode *node, int64_t offset);

// 0-based position of `node` in the whole tree. O(log n).
int64_t avl_rank(const AVLNode *node);
