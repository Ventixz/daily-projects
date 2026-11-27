#include "avl.h"

#include <algorithm>

static void avl_update(AVLNode *node) {
    node->height = 1 + std::max(avl_height(node->left), avl_height(node->right));
    node->cnt = 1 + avl_cnt(node->left) + avl_cnt(node->right);
}

/*     node              new_node
 *    /    \             /      \
 *   a   new_node  =>  node      c
 *       /    \        /  \
 *    inner    c      a  inner
 */
static AVLNode *rot_left(AVLNode *node) {
    AVLNode *parent = node->parent;
    AVLNode *new_node = node->right;
    AVLNode *inner = new_node->left;
    node->right = inner;
    if (inner) inner->parent = node;
    new_node->parent = parent;  // caller re-points parent's child link
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

// Left subtree is 2 taller than the right one.
static AVLNode *avl_fix_left(AVLNode *node) {
    // Left-right case: turn it into the left-left case first.
    if (avl_height(node->left->left) < avl_height(node->left->right)) {
        node->left = rot_left(node->left);
    }
    return rot_right(node);
}

static AVLNode *avl_fix_right(AVLNode *node) {
    if (avl_height(node->right->right) < avl_height(node->right->left)) {
        node->right = rot_right(node->right);
    }
    return rot_left(node);
}

AVLNode *avl_fix(AVLNode *node) {
    while (true) {
        AVLNode **from = &node;  // where the (possibly rotated) subtree hangs
        AVLNode *parent = node->parent;
        if (parent) from = parent->left == node ? &parent->left : &parent->right;
        avl_update(node);
        uint32_t l = avl_height(node->left);
        uint32_t r = avl_height(node->right);
        if (l == r + 2) {
            *from = avl_fix_left(node);
        } else if (l + 2 == r) {
            *from = avl_fix_right(node);
        }
        if (!parent) return *from;
        node = parent;
    }
}

// Removes a node that has at most one child.
static AVLNode *avl_del_easy(AVLNode *node) {
    AVLNode *child = node->left ? node->left : node->right;
    AVLNode *parent = node->parent;
    if (child) child->parent = parent;
    if (!parent) return child;
    AVLNode **from = parent->left == node ? &parent->left : &parent->right;
    *from = child;
    return avl_fix(parent);
}

AVLNode *avl_del(AVLNode *node) {
    if (!node->left || !node->right) return avl_del_easy(node);
    // Two children: detach the in-order successor, then put it where `node` was.
    AVLNode *victim = node->right;
    while (victim->left) victim = victim->left;
    AVLNode *root = avl_del_easy(victim);
    // `node` may have been rotated while fixing up, so copy its links only now.
    *victim = *node;
    if (victim->left) victim->left->parent = victim;
    if (victim->right) victim->right->parent = victim;
    AVLNode **from = &root;
    AVLNode *parent = node->parent;
    if (parent) from = parent->left == node ? &parent->left : &parent->right;
    *from = victim;
    return root;
}

AVLNode *avl_offset(AVLNode *node, int64_t offset) {
    int64_t pos = 0;  // rank of `node` relative to the starting node
    while (offset != pos) {
        if (pos < offset && pos + avl_cnt(node->right) >= offset) {
            node = node->right;  // target is inside the right subtree
            pos += avl_cnt(node->left) + 1;
        } else if (pos > offset && pos - avl_cnt(node->left) <= offset) {
            node = node->left;  // target is inside the left subtree
            pos -= avl_cnt(node->right) + 1;
        } else {
            AVLNode *parent = node->parent;  // go up and look wider
            if (!parent) return nullptr;
            if (parent->right == node) {
                pos -= avl_cnt(node->left) + 1;
            } else {
                pos += avl_cnt(node->right) + 1;
            }
            node = parent;
        }
    }
    return node;
}

int64_t avl_rank(const AVLNode *node) {
    int64_t rank = avl_cnt(node->left);
    for (; node->parent; node = node->parent) {
        if (node->parent->right == node) rank += avl_cnt(node->parent->left) + 1;
    }
    return rank;
}
