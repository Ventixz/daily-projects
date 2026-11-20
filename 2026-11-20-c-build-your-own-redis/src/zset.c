#include "zset.h"
#include "common.h"

#include <stdio.h>
#include <stdlib.h>

static ZNode *znode_new(const char *name, size_t len, double score) {
    ZNode *node = malloc(sizeof(ZNode) + len);
    if (!node) { perror("malloc"); abort(); }
    avl_init(&node->tree);
    node->hmap.next = NULL;
    node->hmap.hcode = str_hash((const uint8_t *)name, len);
    node->score = score;
    node->len = len;
    memcpy(node->name, name, len);
    return node;
}

/* (score, name) < (other score, other name) — the tree's ordering. */
static bool zless(const ZNode *a, double score, const char *name, size_t len) {
    if (a->score != score) return a->score < score;
    size_t n = a->len < len ? a->len : len;
    int rv = memcmp(a->name, name, n);
    if (rv != 0) return rv < 0;
    return a->len < len;
}

static bool zless_node(const ZNode *a, const ZNode *b) {
    return zless(a, b->score, b->name, b->len);
}

static void tree_insert(ZSet *zset, ZNode *node) {
    AVLNode *parent = NULL;
    AVLNode **from = &zset->root;
    while (*from) {
        parent = *from;
        from = zless_node(node, container_of(parent, ZNode, tree)) ? &parent->left : &parent->right;
    }
    *from = &node->tree;
    node->tree.parent = parent;
    zset->root = avl_fix(&node->tree);
}

/* Lookup key for the hashtable: points at a name that is not inside a ZNode. */
typedef struct {
    HNode node;
    const char *name;
    size_t len;
} HKey;

static bool hcmp(const HNode *node, const HNode *key) {
    const ZNode *z = container_of(node, ZNode, hmap);
    const HKey *k = container_of(key, HKey, node);
    return z->len == k->len && memcmp(z->name, k->name, z->len) == 0;
}

ZNode *zset_lookup(ZSet *zset, const char *name, size_t len) {
    if (!zset->root) return NULL;
    HKey key = {.node = {.next = NULL, .hcode = str_hash((const uint8_t *)name, len)}, .name = name, .len = len};
    HNode *found = hm_lookup(&zset->hmap, &key.node, hcmp);
    return found ? container_of(found, ZNode, hmap) : NULL;
}

bool zset_insert(ZSet *zset, const char *name, size_t len, double score) {
    ZNode *node = zset_lookup(zset, name, len);
    if (node) {
        if (node->score != score) {
            /* The tree position depends on the score: unlink, update, relink. */
            zset->root = avl_del(&node->tree);
            avl_init(&node->tree);
            node->score = score;
            tree_insert(zset, node);
        }
        return false;
    }
    node = znode_new(name, len, score);
    hm_insert(&zset->hmap, &node->hmap);
    tree_insert(zset, node);
    return true;
}

void zset_delete(ZSet *zset, ZNode *node) {
    HKey key = {.node = {.next = NULL, .hcode = node->hmap.hcode}, .name = node->name, .len = node->len};
    HNode *found = hm_delete(&zset->hmap, &key.node, hcmp);
    (void)found;
    zset->root = avl_del(&node->tree);
    free(node);
}

ZNode *zset_seekge(ZSet *zset, double score, const char *name, size_t len) {
    AVLNode *found = NULL;
    for (AVLNode *node = zset->root; node;) {
        if (zless(container_of(node, ZNode, tree), score, name, len)) {
            node = node->right;
        } else {
            found = node;  /* candidate; something smaller may still qualify */
            node = node->left;
        }
    }
    return found ? container_of(found, ZNode, tree) : NULL;
}

ZNode *znode_offset(ZNode *node, int64_t offset) {
    AVLNode *t = node ? avl_offset(&node->tree, offset) : NULL;
    return t ? container_of(t, ZNode, tree) : NULL;
}

int64_t znode_rank(const ZNode *node) { return avl_rank(&node->tree); }

size_t zset_size(const ZSet *zset) { return avl_cnt(zset->root); }

static void tree_dispose(AVLNode *node) {
    if (!node) return;
    tree_dispose(node->left);   /* depth is O(log n): recursion is safe */
    tree_dispose(node->right);
    free(container_of(node, ZNode, tree));
}

void zset_clear(ZSet *zset) {
    hm_clear(&zset->hmap);
    tree_dispose(zset->root);
    zset->root = NULL;
}
