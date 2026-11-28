#include <algorithm>
#include <cstdlib>
#include <memory>
#include <random>
#include <set>
#include <vector>

#include "avl.h"
#include "testing.h"

namespace {

struct TNode : AVLNode {
    uint32_t val;
    explicit TNode(uint32_t v) : val(v) {}
};

uint32_t val_of(const AVLNode *n) { return static_cast<const TNode *>(n)->val; }

struct Tree {
    AVLNode *root = nullptr;
    std::vector<std::unique_ptr<TNode>> owned;

    TNode *add(uint32_t v) {
        owned.push_back(std::make_unique<TNode>(v));
        TNode *node = owned.back().get();
        AVLNode *parent = nullptr;
        AVLNode **from = &root;
        while (*from) {
            parent = *from;
            from = v < val_of(parent) ? &parent->left : &parent->right;
        }
        *from = node;
        node->parent = parent;
        root = avl_fix(node);
        return node;
    }

    TNode *find(uint32_t v) {
        AVLNode *n = root;
        while (n && val_of(n) != v) n = v < val_of(n) ? n->left : n->right;
        return static_cast<TNode *>(n);
    }

    void del(uint32_t v) {
        TNode *n = find(v);
        CHECK(n != nullptr);
        root = avl_del(n);
    }
};

// Checks every structural invariant and returns the subtree's values in order.
void verify(const AVLNode *parent, const AVLNode *n, std::vector<uint32_t> &out) {
    if (!n) return;
    CHECK(n->parent == parent);
    verify(n, n->left, out);
    out.push_back(val_of(n));
    verify(n, n->right, out);
    uint32_t l = avl_height(n->left), r = avl_height(n->right);
    CHECK_EQ(n->height, 1 + std::max(l, r));
    CHECK(l <= r + 1 && r <= l + 1);  // the AVL balance condition
    CHECK_EQ(n->cnt, 1 + avl_cnt(n->left) + avl_cnt(n->right));
}

void verify_tree(const Tree &t, const std::multiset<uint32_t> &ref) {
    std::vector<uint32_t> got;
    verify(nullptr, t.root, got);
    CHECK(std::is_sorted(got.begin(), got.end()));
    CHECK(std::equal(got.begin(), got.end(), ref.begin(), ref.end()));
}

}  // namespace

TEST(avl_sequential_inserts_stay_balanced) {
    // Sorted input is the worst case for an unbalanced BST (it becomes a list).
    Tree t;
    std::multiset<uint32_t> ref;
    for (uint32_t i = 0; i < 1000; i++) {
        t.add(i);
        ref.insert(i);
    }
    verify_tree(t, ref);
    CHECK(t.root->height <= 15);  // AVL bound: < 1.44 * log2(1000 + 2)
}

TEST(avl_random_insert_delete_keeps_invariants) {
    std::mt19937 rng(7);
    for (int round = 0; round < 50; round++) {
        Tree t;
        std::multiset<uint32_t> ref;
        size_t n = 1 + rng() % 200;
        for (size_t i = 0; i < n; i++) {
            uint32_t v = rng() % 100;  // duplicates on purpose
            t.add(v);
            ref.insert(v);
            verify_tree(t, ref);
        }
        while (!ref.empty()) {
            auto it = ref.begin();
            std::advance(it, rng() % ref.size());
            t.del(*it);
            ref.erase(it);
            verify_tree(t, ref);
        }
        CHECK(t.root == nullptr);
    }
}

TEST(avl_offset_and_rank_from_every_node) {
    Tree t;
    std::vector<TNode *> sorted;
    for (uint32_t i = 0; i < 300; i++) t.add((i * 37) % 300);  // a permutation of 0..299
    for (uint32_t v = 0; v < 300; v++) sorted.push_back(t.find(v));
    for (int64_t i = 0; i < 300; i++) {
        CHECK_EQ(avl_rank(sorted[i]), i);
        for (int64_t j = 0; j < 300; j++) {
            CHECK(avl_offset(sorted[i], j - i) == sorted[j]);
        }
        CHECK(avl_offset(sorted[i], 300 - i) == nullptr);
        CHECK(avl_offset(sorted[i], -i - 1) == nullptr);
    }
}
