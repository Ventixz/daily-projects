// Intrusive AVL tree augmented with subtree sizes.
//
// The size field (`cnt`) is what turns a plain balanced tree into an
// order-statistic tree: rank queries and "skip N items" become O(log n)
// instead of O(n). Sorted sets use this for ZRANK, ZRANGE and offset/limit.
#pragma once

#include <cstddef>
#include <cstdint>

struct AVLNode {
    AVLNode *parent = nullptr;
    AVLNode *left = nullptr;
    AVLNode *right = nullptr;
    uint32_t height = 1;  // height of this subtree
    uint32_t cnt = 1;     // number of nodes in this subtree
};

inline void avl_init(AVLNode *node) { *node = AVLNode{}; }
inline uint32_t avl_height(const AVLNode *node) { return node ? node->height : 0; }
inline uint32_t avl_cnt(const AVLNode *node) { return node ? node->cnt : 0; }

// After linking `node` in as a leaf (or changing the tree below it), walk up
// to the root restoring heights, counts and balance. Returns the new root.
AVLNode *avl_fix(AVLNode *node);
// Unlink `node`; returns the new root (nullptr if the tree became empty).
AVLNode *avl_del(AVLNode *node);
// The node `offset` positions away in sorted order (negative = backwards),
// or nullptr if that runs off either end.
AVLNode *avl_offset(AVLNode *node, int64_t offset);
// 0-based position of `node` in sorted order.
int64_t avl_rank(const AVLNode *node);
// The node at 0-based position `k` under `root`, or nullptr.
AVLNode *avl_at(AVLNode *root, int64_t k);
