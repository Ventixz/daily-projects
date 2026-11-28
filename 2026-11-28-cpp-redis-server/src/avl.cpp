#include "avl.h"

#include <algorithm>
#include <cassert>

namespace {

void avl_update(AVLNode *n) {
    n->height = 1 + std::max(avl_height(n->left), avl_height(n->right));
    n->cnt = 1 + avl_cnt(n->left) + avl_cnt(n->right);
}

/*   node              new_node
 *   /  \               /    \
 *  a   new_node  =>  node    c
 *       /  \         /  \
 *    inner  c       a  inner
 */
AVLNode *rot_left(AVLNode *node) {
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

AVLNode *rot_right(AVLNode *node) {
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

// Left subtree is 2 taller. If the extra height is on the left child's *right*
// side, a single right rotation would just move the problem, so rotate the
// child first (the "left-right" case).
AVLNode *avl_fix_left(AVLNode *node) {
    if (avl_height(node->left->left) < avl_height(node->left->right)) {
        node->left = rot_left(node->left);
    }
    return rot_right(node);
}

AVLNode *avl_fix_right(AVLNode *node) {
    if (avl_height(node->right->right) < avl_height(node->right->left)) {
        node->right = rot_right(node->right);
    }
    return rot_left(node);
}

// Remove a node that has at most one child: splice the child into its place.
AVLNode *avl_del_easy(AVLNode *node) {
    assert(!node->left || !node->right);
    AVLNode *child = node->left ? node->left : node->right;
    AVLNode *parent = node->parent;
    if (child) child->parent = parent;
    if (!parent) return child;
    AVLNode **from = parent->left == node ? &parent->left : &parent->right;
    *from = child;
    return avl_fix(parent);
}

}  // namespace

AVLNode *avl_fix(AVLNode *node) {
    while (true) {
        AVLNode **from = &node;  // where the (possibly rotated) subtree gets attached
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

AVLNode *avl_del(AVLNode *node) {
    if (!node->left || !node->right) return avl_del_easy(node);
    // Two children: unlink the in-order successor (it has no left child), then
    // put it exactly where `node` was by copying node's links into it.
    AVLNode *victim = node->right;
    while (victim->left) victim = victim->left;
    AVLNode *root = avl_del_easy(victim);
    *victim = *node;  // copies only the AVLNode part: links, height, cnt
    if (victim->left) victim->left->parent = victim;
    if (victim->right) victim->right->parent = victim;
    AVLNode **from = &root;
    AVLNode *parent = node->parent;
    if (parent) from = parent->left == node ? &parent->left : &parent->right;
    *from = victim;
    return root;
}

AVLNode *avl_offset(AVLNode *node, int64_t offset) {
    int64_t pos = 0;  // position relative to the starting node
    while (offset != pos) {
        if (pos < offset && pos + avl_cnt(node->right) >= offset) {
            // target is inside the right subtree
            node = node->right;
            pos += avl_cnt(node->left) + 1;
        } else if (pos > offset && pos - avl_cnt(node->left) <= offset) {
            // target is inside the left subtree
            node = node->left;
            pos -= avl_cnt(node->right) + 1;
        } else {
            // not below us: climb, adjusting pos by the subtree we leave behind
            AVLNode *parent = node->parent;
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
    for (const AVLNode *n = node; n->parent; n = n->parent) {
        if (n->parent->right == n) rank += avl_cnt(n->parent->left) + 1;
    }
    return rank;
}
