// Binary min-heap of expiry times, with back-references.
//
// A key's TTL can change or be removed at any time (PEXPIRE, PERSIST, SET, DEL),
// so each heap item points at its owner's HeapNode and every move inside the
// heap writes the new index back into that node. That turns "update this
// key's expiry" from an O(n) search into an O(log n) sift.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

constexpr size_t k_not_in_heap = SIZE_MAX;

struct HeapNode {
    size_t heap_idx = k_not_in_heap;
};

struct HeapItem {
    uint64_t val;    // absolute expiry time, ms on the monotonic clock
    HeapNode *node;  // owner; node->heap_idx always equals this item's index
};

// Insert `node` with `val`, or change its value if it is already in the heap.
void heap_set(std::vector<HeapItem> &heap, HeapNode *node, uint64_t val);
// Take `node` out of the heap. No-op if it is not in it.
void heap_remove(std::vector<HeapItem> &heap, HeapNode *node);
