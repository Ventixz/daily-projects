/* Intrusive AVL tree, augmented with subtree sizes so that rank and
 * "the node k places after this one" are O(log n). The sorted set needs both:
 * ZRANK and offset-based range queries. */
#ifndef AVL_H
#define AVL_H

#include <stddef.h>
#include <stdint.h>

typedef struct AVLNode {
    struct AVLNode *parent, *left, *right;
    uint32_t height;  /* 1 for a leaf */
    uint32_t cnt;     /* nodes in this subtree, including itself */
} AVLNode;

static inline void avl_init(AVLNode *node) {
    node->parent = node->left = node->right = NULL;
    node->height = 1;
    node->cnt = 1;
}

static inline uint32_t avl_height(const AVLNode *n) { return n ? n->height : 0; }
static inline uint32_t avl_cnt(const AVLNode *n) { return n ? n->cnt : 0; }

/* After linking a new leaf (or changing a subtree), restore heights, counts and
 * balance from `node` up to the root. Returns the new root. */
AVLNode *avl_fix(AVLNode *node);
/* Unlink `node` from its tree. Returns the new root (NULL if now empty). */
AVLNode *avl_del(AVLNode *node);
/* The node `offset` positions after `node` in sorted order (negative = before),
 * or NULL if that falls outside the tree. */
AVLNode *avl_offset(AVLNode *node, int64_t offset);
/* Zero-based position of `node` in its whole tree. */
int64_t avl_rank(const AVLNode *node);

#endif
