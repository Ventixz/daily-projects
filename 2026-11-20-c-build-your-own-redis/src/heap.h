/* Binary min-heap of expiry times. Each item carries a pointer back to the
 * owner's stored index, which the heap keeps up to date as items move, so an
 * owner can remove or reschedule its own item in O(log n). */
#ifndef HEAP_H
#define HEAP_H

#include <stddef.h>
#include <stdint.h>

#define HEAP_NONE ((size_t)-1)

typedef struct {
    uint64_t val;  /* expiry time, ms */
    size_t *ref;   /* where the owner stores this item's index */
} HeapItem;

typedef struct {
    HeapItem *items;
    size_t len, cap;
} Heap;

/* Insert, or reschedule if *ref already names a slot. */
void heap_upsert(Heap *h, size_t *ref, uint64_t val);
/* Remove the item at *ref (if any) and set *ref to HEAP_NONE. */
void heap_remove(Heap *h, size_t *ref);
static inline const HeapItem *heap_top(const Heap *h) { return h->len ? &h->items[0] : NULL; }
void heap_free(Heap *h);

#endif
