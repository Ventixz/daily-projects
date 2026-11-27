// Binary min-heap of deadlines that knows where each item lives.
//
// Every item carries `ref`, a pointer to a size_t owned by whoever inserted
// it. The heap writes the item's current index there each time it moves the
// item, so the owner can update or remove "its" deadline in O(log n) without
// searching. This is how a key's TTL is changed or cleared.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

struct HeapItem {
    uint64_t val = 0;       // deadline (ms)
    size_t *ref = nullptr;  // owner's copy of this item's index
    void *data = nullptr;   // owner pointer, for the expiry loop
};

constexpr size_t k_heap_none = SIZE_MAX;

// Restores heap order after a[pos] changed.
void heap_update(HeapItem *a, size_t pos, size_t len);
// Inserts (pos == k_heap_none) or replaces the item at `pos`.
void heap_upsert(std::vector<HeapItem> &a, size_t pos, HeapItem item);
// Removes the item at `pos` and sets its ref to k_heap_none.
void heap_delete(std::vector<HeapItem> &a, size_t pos);
