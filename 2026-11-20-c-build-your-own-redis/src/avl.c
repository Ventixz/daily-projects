#include "avl.h"

#include <stddef.h>

static uint32_t max_u32(uint32_t a, uint32_t b) { return a > b ? a : b; }

static void avl_update(AVLNode *n) {
    n->height = 1 + max_u32(avl_height(n->left), avl_height(n->right));
    n->cnt = 1 + avl_cnt(n->left) + avl_cnt(n->right);
}

/*      node                new
 *     /    \              /   \
 *    a     new    =>   node    c
 *         /   \        /  \
 *     inner    c      a   inner
 * The caller re-links the returned subtree root into the parent. */
static AVLNode *rot_left(AVLNode *node) {
    AVLNode *parent = node->parent;
    AVLNode *new_node = node->right;
    AVLNode *inner = new_node->left;
    node->right = inner;
    if (inner) inner->parent = node;
    new_node->parent = parent;
    new_node->left = node;
    node->parent = new_node;
    avl_update(node);
    avl_update(new_node);
    return new_node;
}

static AVLNode *rot_right(AVLNode *node) {
    AVLNode *parent = node->parent;
    AVLNode *new_node = node->left;
    AVLNode *inner = new_node->right;
    node->left = inner;
    if (inner) inner->parent = node;
    new_node->parent = parent;
    new_node->right = node;
    node->parent = new_node;
    avl_update(node);
    avl_update(new_node);
    return new_node;
}

/* Left subtree is 2 taller. If its own right side is the taller one (the
 * "left-right" case) a single rotation would not fix it, so rotate that child
 * first. */
static AVLNode *fix_left(AVLNode *node) {
    if (avl_height(node->left->left) < avl_height(node->left->right)) {
        node->left = rot_left(node->left);
    }
    return rot_right(node);
}

static AVLNode *fix_right(AVLNode *node) {
    if (avl_height(node->right->right) < avl_height(node->right->left)) {
        node->right = rot_right(node->right);
    }
    return rot_left(node);
}

AVLNode *avl_fix(AVLNode *node) {
    for (;;) {
        AVLNode *parent = node->parent;
        AVLNode **from = &node;
        if (parent) from = parent->left == node ? &parent->left : &parent->right;
        avl_update(node);
        uint32_t l = avl_height(node->left), r = avl_height(node->right);
        if (l == r + 2) *from = fix_left(node);
        else if (l + 2 == r) *from = fix_right(node);
        if (!parent) return *from;
        node = parent;
    }
}

/* Remove a node that has at most one child: splice the child into its place. */
static AVLNode *avl_del_easy(AVLNode *node) {
    AVLNode *child = node->left ? node->left : node->right;
    AVLNode *parent = node->parent;
    if (child) child->parent = parent;
    if (!parent) return child;
    if (parent->left == node) parent->left = child;
    else parent->right = child;
    return avl_fix(parent);
}

AVLNode *avl_del(AVLNode *node) {
    if (!node->left || !node->right) return avl_del_easy(node);
    /* Two children: detach the in-order successor (which has no left child),
     * then put it exactly where `node` sits now. Rotations during the detach
     * may have moved `node`, so read its links only afterwards. */
    AVLNode *victim = node->right;
    while (victim->left) victim = victim->left;
    AVLNode *root = avl_del_easy(victim);
    *victim = *node;
    if (victim->left) victim->left->parent = victim;
    if (victim->right) victim->right->parent = victim;
    AVLNode *parent = node->parent;
    if (!parent) return victim;
    if (parent->left == node) parent->left = victim;
    else parent->right = victim;
    return root;
}

AVLNode *avl_offset(AVLNode *node, int64_t offset) {
    int64_t pos = 0;  /* position of `node` relative to the starting node */
    while (offset != pos) {
        if (pos < offset && pos + (int64_t)avl_cnt(node->right) >= offset) {
            node = node->right;
            pos += (int64_t)avl_cnt(node->left) + 1;
        } else if (pos > offset && pos - (int64_t)avl_cnt(node->left) <= offset) {
            node = node->left;
            pos -= (int64_t)avl_cnt(node->right) + 1;
        } else {
            AVLNode *parent = node->parent;
            if (!parent) return NULL;
            if (parent->right == node) pos -= (int64_t)avl_cnt(node->left) + 1;
            else pos += (int64_t)avl_cnt(node->right) + 1;
            node = parent;
        }
    }
    return node;
}

int64_t avl_rank(const AVLNode *node) {
    int64_t rank = avl_cnt(node->left);
    for (; node->parent; node = node->parent) {
        if (node->parent->right == node) rank += (int64_t)avl_cnt(node->parent->left) + 1;
    }
    return rank;
}
