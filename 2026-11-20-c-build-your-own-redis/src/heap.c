#include "heap.h"

#include <stdio.h>
#include <stdlib.h>

static void set(Heap *h, size_t pos, HeapItem item) {
    h->items[pos] = item;
    *item.ref = pos;
}

static void sift_up(Heap *h, size_t pos) {
    HeapItem t = h->items[pos];
    while (pos > 0) {
        size_t parent = (pos - 1) / 2;
        if (h->items[parent].val <= t.val) break;
        set(h, pos, h->items[parent]);
        pos = parent;
    }
    set(h, pos, t);
}

static void sift_down(Heap *h, size_t pos) {
    HeapItem t = h->items[pos];
    for (;;) {
        size_t l = pos * 2 + 1, r = l + 1, min = pos;
        uint64_t min_val = t.val;
        if (l < h->len && h->items[l].val < min_val) { min = l; min_val = h->items[l].val; }
        if (r < h->len && h->items[r].val < min_val) min = r;
        if (min == pos) break;
        set(h, pos, h->items[min]);
        pos = min;
    }
    set(h, pos, t);
}

static void heap_fix(Heap *h, size_t pos) {
    if (pos > 0 && h->items[(pos - 1) / 2].val > h->items[pos].val) sift_up(h, pos);
    else sift_down(h, pos);
}

void heap_upsert(Heap *h, size_t *ref, uint64_t val) {
    if (*ref != HEAP_NONE) {
        h->items[*ref].val = val;
        heap_fix(h, *ref);
        return;
    }
    if (h->len == h->cap) {
        size_t cap = h->cap ? h->cap * 2 : 16;
        HeapItem *items = realloc(h->items, cap * sizeof(HeapItem));
        if (!items) { perror("realloc"); abort(); }
        h->items = items;
        h->cap = cap;
    }
    size_t pos = h->len++;
    set(h, pos, (HeapItem){.val = val, .ref = ref});
    sift_up(h, pos);
}

void heap_remove(Heap *h, size_t *ref) {
    size_t pos = *ref;
    if (pos == HEAP_NONE) return;
    *ref = HEAP_NONE;
    h->len--;
    if (pos == h->len) return;          /* removed the last slot */
    set(h, pos, h->items[h->len]);      /* move the last item into the hole */
    heap_fix(h, pos);
}

void heap_free(Heap *h) {
    free(h->items);
    *h = (Heap){0};
}
