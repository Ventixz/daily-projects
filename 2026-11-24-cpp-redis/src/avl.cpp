#include "avl.h"

#include <algorithm>

namespace {

void avl_update(AVLNode *node) {
    node->height = 1 + std::max(avl_height(node->left), avl_height(node->right));
    node->cnt = 1 + avl_cnt(node->left) + avl_cnt(node->right);
}

// Left rotation: node's right child becomes the subtree root, and that
// child's old left subtree ("inner") is re-hung as node's right subtree.
//
//     node                 new_node
//    /    |               /        |
//   a   new_node   =>   node       c
//       /     |        /    |
//    inner    c       a    inner
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

// Left subtree is 2 taller than the right one.
AVLNode *avl_fix_left(AVLNode *node) {
    // Left-right case: turn it into left-left first.
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

// Remove a node that has at most one child.
AVLNode *avl_del_easy(AVLNode *node) {
    AVLNode *child = node->left ? node->left : node->right;
    AVLNode *parent = node->parent;
    if (child) child->parent = parent;
    if (!parent) return child;  // removed the root
    AVLNode **from = parent->left == node ? &parent->left : &parent->right;
    *from = child;
    return avl_fix(parent);
}

}  // namespace

AVLNode *avl_fix(AVLNode *node) {
    while (true) {
        AVLNode **from = &node;  // where to store the (possibly rotated) subtree
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
    // Two children: detach the in-order successor, then put it where `node` was.
    AVLNode *victim = node->right;
    while (victim->left) victim = victim->left;
    AVLNode *root = avl_del_easy(victim);
    *victim = *node;  // takes over links, height and count
    if (victim->left) victim->left->parent = victim;
    if (victim->right) victim->right->parent = victim;
    AVLNode **from = &root;
    if (AVLNode *parent = node->parent) {
        from = parent->left == node ? &parent->left : &parent->right;
    }
    *from = victim;
    return root;
}

AVLNode *avl_offset(AVLNode *node, int64_t offset) {
    int64_t pos = 0;  // position relative to the starting node
    while (offset != pos) {
        if (pos < offset && pos + avl_cnt(node->right) >= offset) {
            node = node->right;  // target is inside the right subtree
            pos += avl_cnt(node->left) + 1;
        } else if (pos > offset && pos - avl_cnt(node->left) <= offset) {
            node = node->left;  // target is inside the left subtree
            pos -= avl_cnt(node->right) + 1;
        } else {
            AVLNode *parent = node->parent;  // go up
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
    for (const AVLNode *cur = node; cur->parent; cur = cur->parent) {
        if (cur->parent->right == cur) rank += avl_cnt(cur->parent->left) + 1;
    }
    return rank;
}

AVLNode *avl_at(AVLNode *root, int64_t k) {
    if (k < 0 || k >= avl_cnt(root)) return nullptr;
    AVLNode *node = root;
    while (true) {
        int64_t left = avl_cnt(node->left);
        if (k < left) {
            node = node->left;
        } else if (k == left) {
            return node;
        } else {
            k -= left + 1;
            node = node->right;
        }
    }
}
