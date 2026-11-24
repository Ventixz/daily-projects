#include "zset.h"

namespace {

// A stack-allocated probe for hash lookups: only hcode and name are used.
struct HKey : HNode {
    std::string_view name;
};

bool hcmp(const HNode *node, const HNode *key) {
    return static_cast<const ZNode *>(node)->name == static_cast<const HKey *>(key)->name;
}

uint64_t name_hash(std::string_view name) {
    return str_hash(reinterpret_cast<const uint8_t *>(name.data()), name.size());
}

ZNode *as_znode(AVLNode *n) { return n ? static_cast<ZNode *>(n) : nullptr; }

// Order by score, ties broken by name (byte-wise), exactly like Redis.
bool zless(const ZNode *a, double score, std::string_view name) {
    if (a->score != score) return a->score < score;
    return std::string_view(a->name) < name;
}

bool zless(const ZNode *a, const ZNode *b) { return zless(a, b->score, b->name); }

void free_tree(AVLNode *node) {
    if (!node) return;
    free_tree(node->left);
    free_tree(node->right);
    delete static_cast<ZNode *>(node);
}

}  // namespace

void ZSet::tree_insert(ZNode *node) {
    AVLNode *parent = nullptr;
    AVLNode **from = &root_;
    while (*from) {
        parent = *from;
        from = zless(node, as_znode(parent)) ? &parent->left : &parent->right;
    }
    *from = node;
    node->parent = parent;
    root_ = avl_fix(node);
}

void ZSet::update_score(ZNode *node, double score) {
    if (node->score == score) return;
    root_ = avl_del(node);
    avl_init(node);
    node->score = score;
    tree_insert(node);
}

bool ZSet::insert(std::string_view name, double score) {
    if (ZNode *node = lookup(name)) {
        update_score(node, score);
        return false;
    }
    auto *node = new ZNode;
    node->name = std::string(name);
    node->score = score;
    node->hcode = name_hash(name);
    hm_insert(&hmap_, node);
    tree_insert(node);
    return true;
}

ZNode *ZSet::lookup(std::string_view name) {
    if (!root_) return nullptr;
    HKey key;
    key.hcode = name_hash(name);
    key.name = name;
    HNode *found = hm_lookup(&hmap_, &key, &hcmp);
    return found ? static_cast<ZNode *>(found) : nullptr;
}

void ZSet::remove(ZNode *node) {
    HKey key;
    key.hcode = node->hcode;
    key.name = node->name;
    hm_delete(&hmap_, &key, &hcmp);
    root_ = avl_del(node);
    delete node;
}

ZNode *ZSet::seek_ge(double score, std::string_view name) {
    AVLNode *found = nullptr;
    for (AVLNode *node = root_; node;) {
        if (zless(as_znode(node), score, name)) {
            node = node->right;
        } else {
            found = node;  // candidate; look for a smaller one on the left
            node = node->left;
        }
    }
    return as_znode(found);
}

ZNode *ZSet::at(int64_t rank) { return as_znode(avl_at(root_, rank)); }

ZNode *ZSet::offset(ZNode *node, int64_t off) { return node ? as_znode(avl_offset(node, off)) : nullptr; }

void ZSet::clear() {
    hm_clear(&hmap_);
    free_tree(root_);
    root_ = nullptr;
}
