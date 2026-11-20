/* Sorted set: every member is in two indexes at once.
 *   - a hashtable keyed by name   -> O(1) ZSCORE / ZREM lookups
 *   - an AVL tree ordered by (score, name) -> ordered range queries and ranks */
#ifndef ZSET_H
#define ZSET_H

#include "avl.h"
#include "hashtable.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    AVLNode *root;
    HMap hmap;
} ZSet;

typedef struct {
    AVLNode tree;
    HNode hmap;
    double score;
    size_t len;
    char name[];  /* not NUL-terminated; names are binary-safe */
} ZNode;

/* Adds name with score, or updates the score. Returns true if it was new. */
bool zset_insert(ZSet *zset, const char *name, size_t len, double score);
ZNode *zset_lookup(ZSet *zset, const char *name, size_t len);
void zset_delete(ZSet *zset, ZNode *node);
/* First member >= (score, name), or NULL. */
ZNode *zset_seekge(ZSet *zset, double score, const char *name, size_t len);
ZNode *znode_offset(ZNode *node, int64_t offset);
int64_t znode_rank(const ZNode *node);
size_t zset_size(const ZSet *zset);
void zset_clear(ZSet *zset);

#endif
