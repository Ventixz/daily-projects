#include "zset.h"

#include <cstring>

static ZNode *znode_new(std::string_view name, double score) {
    ZNode *node = new ZNode;
    node->hcode = str_hash(name.data(), name.size());
    node->score = score;
    node->len = name.size();
    node->name = new char[name.size() ? name.size() : 1];
    memcpy(node->name, name.data(), name.size());
    return node;
}

static void znode_del(ZNode *node) {
    delete[] node->name;
    delete node;
}

static ZNode *as_znode(AVLNode *node) { return static_cast<ZNode *>(node); }

// Is `lhs` ordered before the (score, name) pair?
static bool zless(const ZNode *lhs, double score, std::string_view name) {
    if (lhs->score != score) return lhs->score < score;
    return znode_name(lhs) < name;
}

static void tree_insert(ZSet *zset, ZNode *node) {
    AVLNode *parent = nullptr;
    AVLNode **from = &zset->root;
    while (*from) {
        parent = *from;
        from = zless(node, as_znode(parent)->score, znode_name(as_znode(parent)))
                   ? &parent->left
                   : &parent->right;
    }
    *from = node;
    node->parent = parent;
    zset->root = avl_fix(node);
}

namespace {
struct NameKey : HNode {
    std::string_view name;
};
}  // namespace

static bool name_eq(HNode *stored, HNode *key) {
    return znode_name(static_cast<ZNode *>(stored)) == static_cast<NameKey *>(key)->name;
}

ZNode *zset_lookup(ZSet *zset, std::string_view name) {
    if (!zset->root) return nullptr;
    NameKey key;
    key.hcode = str_hash(name.data(), name.size());
    key.name = name;
    HNode *found = hm_lookup(&zset->hmap, &key, &name_eq);
    return found ? static_cast<ZNode *>(found) : nullptr;
}

bool zset_insert(ZSet *zset, std::string_view name, double score) {
    if (ZNode *node = zset_lookup(zset, name)) {
        if (node->score != score) {
            // Changing the sort key: take it out of the tree and put it back.
            zset->root = avl_del(node);
            avl_init(node);
            node->score = score;
            tree_insert(zset, node);
        }
        return false;
    }
    ZNode *node = znode_new(name, score);
    hm_insert(&zset->hmap, node);
    tree_insert(zset, node);
    return true;
}

bool zset_delete(ZSet *zset, std::string_view name) {
    ZNode *node = zset_lookup(zset, name);
    if (!node) return false;
    NameKey key;
    key.hcode = node->hcode;
    key.name = name;
    hm_delete(&zset->hmap, &key, &name_eq);
    zset->root = avl_del(node);
    znode_del(node);
    return true;
}

ZNode *zset_seekge(ZSet *zset, double score, std::string_view name) {
    AVLNode *found = nullptr;
    for (AVLNode *node = zset->root; node;) {
        if (zless(as_znode(node), score, name)) {
            node = node->right;
        } else {
            found = node;  // candidate; something smaller may still qualify
            node = node->left;
        }
    }
    return found ? as_znode(found) : nullptr;
}

ZNode *znode_offset(ZNode *node, int64_t offset) {
    AVLNode *found = node ? avl_offset(node, offset) : nullptr;
    return found ? as_znode(found) : nullptr;
}

int64_t znode_rank(const ZNode *node) { return avl_rank(node); }

size_t zset_size(const ZSet *zset) { return avl_cnt(zset->root); }

static void tree_dispose(AVLNode *node) {
    if (!node) return;  // recursion depth is the tree height: O(log n)
    tree_dispose(node->left);
    tree_dispose(node->right);
    znode_del(as_znode(node));
}

void zset_clear(ZSet *zset) {
    hm_clear(&zset->hmap);
    tree_dispose(zset->root);
    zset->root = nullptr;
}
