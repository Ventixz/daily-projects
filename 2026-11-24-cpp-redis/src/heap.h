// Binary min-heap of expiry times.
//
// Each item carries a pointer back to a size_t inside its owner (the key's
// entry). Whenever an item moves, that index is rewritten, so the owner can
// update or remove its own TTL in O(log n) without searching the heap.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

struct HeapItem {
    uint64_t val = 0;       // expiry time, ms on the monotonic clock
    size_t *ref = nullptr;  // owner's copy of this item's index
    void *owner = nullptr;  // the object that holds *ref
};

constexpr size_t kNoHeapIdx = static_cast<size_t>(-1);

// Restore the heap property for the item at `pos` (moves it up or down).
void heap_update(std::vector<HeapItem> &heap, size_t pos);
// Insert or change the item whose owner index is *ref.
void heap_upsert(std::vector<HeapItem> &heap, size_t *ref, void *owner, uint64_t val);
// Remove the item at *ref (no-op if kNoHeapIdx) and reset *ref.
void heap_remove(std::vector<HeapItem> &heap, size_t *ref);
