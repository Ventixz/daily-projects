/* Intrusive chaining hashtable with progressive (incremental) rehashing.
 *
 * Callers embed an HNode in their own struct and recover it with container_of.
 * When the table outgrows its load factor, a second, larger table is created and
 * every later operation moves a bounded number of nodes across, so no single
 * request ever pays for an O(n) resize. */
#ifndef HASHTABLE_H
#define HASHTABLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct HNode {
    struct HNode *next;
    uint64_t hcode;
} HNode;

typedef struct {
    HNode **tab;   /* array of slots, power-of-two sized */
    size_t mask;   /* slots - 1 */
    size_t size;   /* nodes in this table */
} HTab;

typedef struct {
    HTab newer;
    HTab older;          /* non-empty only while a rehash is in progress */
    size_t migrate_pos;  /* next slot of `older` to migrate */
} HMap;

typedef bool (*HEqFn)(const HNode *a, const HNode *b);

HNode *hm_lookup(HMap *hmap, HNode *key, HEqFn eq);
void hm_insert(HMap *hmap, HNode *node);
HNode *hm_delete(HMap *hmap, HNode *key, HEqFn eq);
void hm_clear(HMap *hmap);  /* frees the slot arrays, not the nodes */
size_t hm_size(const HMap *hmap);
/* Calls f on every node until it returns false. f must not modify the map. */
void hm_foreach(HMap *hmap, bool (*f)(HNode *, void *), void *arg);
bool hm_rehashing(const HMap *hmap);

#endif
