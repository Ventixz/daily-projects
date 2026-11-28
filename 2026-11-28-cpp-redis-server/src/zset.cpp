#include "zset.h"

namespace {

struct ZKey : HNode {
    std::string_view name;
};

bool zeq(const HNode *stored, const HNode *key) {
    return static_cast<const ZNode *>(stored)->name == static_cast<const ZKey *>(key)->name;
}

// Order by score, then by name so equal scores still have a total order.
bool zless(const AVLNode *lhs, double score, std::string_view name) {
    const auto *z = static_cast<const ZNode *>(lhs);
    if (z->score != score) return z->score < score;
    return std::string_view(z->name) < name;
}

bool zless(const AVLNode *lhs, const AVLNode *rhs) {
    const auto *r = static_cast<const ZNode *>(rhs);
    return zless(lhs, r->score, r->name);
}

void tree_dispose(AVLNode *n) {
    if (!n) return;
    tree_dispose(n->left);  // recursion depth is the tree height, ~1.44 log2(n)
    tree_dispose(n->right);
    delete static_cast<ZNode *>(n);
}

}  // namespace

ZNode::ZNode(std::string_view n, double s) : score(s), name(n) {
    hcode = hash_bytes(n.data(), n.size());
}

void ZSet::tree_insert(ZNode *node) {
    AVLNode *parent = nullptr;
    AVLNode **from = &root_;
    while (*from) {
        parent = *from;
        from = zless(node, parent) ? &parent->left : &parent->right;
    }
    *from = node;
    node->parent = parent;
    root_ = avl_fix(node);
}

bool ZSet::insert(std::string_view name, double score) {
    if (ZNode *node = lookup(name)) {
        if (node->score != score) {
            // The tree position depends on the score: take it out, reset its
            // links, and insert it again.
            root_ = avl_del(node);
            static_cast<AVLNode &>(*node) = AVLNode{};
            node->score = score;
            tree_insert(node);
        }
        return false;
    }
    auto *node = new ZNode(name, score);
    hmap_.insert(node);
    tree_insert(node);
    return true;
}

ZNode *ZSet::lookup(std::string_view name) {
    ZKey key;
    key.hcode = hash_bytes(name.data(), name.size());
    key.name = name;
    HNode *found = hmap_.lookup(&key, zeq);
    return found ? static_cast<ZNode *>(found) : nullptr;
}

bool ZSet::remove(std::string_view name) {
    ZKey key;
    key.hcode = hash_bytes(name.data(), name.size());
    key.name = name;
    HNode *found = hmap_.remove(&key, zeq);
    if (!found) return false;
    auto *node = static_cast<ZNode *>(found);
    root_ = avl_del(node);
    delete node;
    return true;
}

ZNode *ZSet::seek_ge(double score, std::string_view name) const {
    AVLNode *found = nullptr;
    for (AVLNode *n = root_; n;) {
        if (zless(n, score, name)) {
            n = n->right;
        } else {
            found = n;  // candidate; something smaller may still be on the left
            n = n->left;
        }
    }
    return found ? static_cast<ZNode *>(found) : nullptr;
}

ZNode *ZSet::offset(ZNode *node, int64_t off) {
    if (!node) return nullptr;
    AVLNode *target = avl_offset(node, off);
    return target ? static_cast<ZNode *>(target) : nullptr;
}

void ZSet::clear() {
    tree_dispose(root_);
    root_ = nullptr;
    hmap_.clear();
}
