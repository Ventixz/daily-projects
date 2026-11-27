// Intrusive AVL tree augmented with subtree sizes.
//
// Every node stores its height (to stay balanced) and the number of nodes in
// its subtree (`cnt`). The counts make "the node k places after this one" and
// "what is this node's rank" O(log n), which is what a sorted set needs for
// ZRANK and for range queries with an offset.
#pragma once
#include <cstdint>

struct AVLNode {
    AVLNode *parent = nullptr;
    AVLNode *left = nullptr;
    AVLNode *right = nullptr;
    uint32_t height = 1;
    uint32_t cnt = 1;
};

inline void avl_init(AVLNode *node) { *node = AVLNode{}; }
inline uint32_t avl_height(const AVLNode *node) { return node ? node->height : 0; }
inline uint32_t avl_cnt(const AVLNode *node) { return node ? node->cnt : 0; }

// Call on a freshly linked leaf (or a node whose subtree changed). Rebalances
// up to the root and returns the new root.
AVLNode *avl_fix(AVLNode *node);
// Unlinks `node` from its tree and returns the new root (may be null).
AVLNode *avl_del(AVLNode *node);
// The node `offset` positions away in sorted order, or null if out of range.
AVLNode *avl_offset(AVLNode *node, int64_t offset);
// 0-based position of `node` in sorted order.
int64_t avl_rank(const AVLNode *node);
