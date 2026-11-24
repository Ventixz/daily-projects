#include "heap.h"

namespace {

size_t heap_parent(size_t i) { return (i + 1) / 2 - 1; }
size_t heap_left(size_t i) { return i * 2 + 1; }

void heap_up(HeapItem *a, size_t pos) {
    HeapItem t = a[pos];
    while (pos > 0 && a[heap_parent(pos)].val > t.val) {
        a[pos] = a[heap_parent(pos)];
        *a[pos].ref = pos;
        pos = heap_parent(pos);
    }
    a[pos] = t;
    *a[pos].ref = pos;
}

void heap_down(HeapItem *a, size_t pos, size_t len) {
    HeapItem t = a[pos];
    while (true) {
        size_t l = heap_left(pos), r = l + 1;
        size_t min_pos = pos;
        uint64_t min_val = t.val;
        if (l < len && a[l].val < min_val) {
            min_pos = l;
            min_val = a[l].val;
        }
        if (r < len && a[r].val < min_val) min_pos = r;
        if (min_pos == pos) break;
        a[pos] = a[min_pos];
        *a[pos].ref = pos;
        pos = min_pos;
    }
    a[pos] = t;
    *a[pos].ref = pos;
}

}  // namespace

void heap_update(std::vector<HeapItem> &heap, size_t pos) {
    if (pos > 0 && heap[heap_parent(pos)].val > heap[pos].val) {
        heap_up(heap.data(), pos);
    } else {
        heap_down(heap.data(), pos, heap.size());
    }
}

void heap_upsert(std::vector<HeapItem> &heap, size_t *ref, void *owner, uint64_t val) {
    size_t pos = *ref;
    if (pos == kNoHeapIdx) {
        pos = heap.size();
        heap.push_back(HeapItem{val, ref, owner});
    } else {
        heap[pos].val = val;
    }
    heap_update(heap, pos);
}

void heap_remove(std::vector<HeapItem> &heap, size_t *ref) {
    size_t pos = *ref;
    if (pos == kNoHeapIdx) return;
    *ref = kNoHeapIdx;
    heap[pos] = heap.back();  // fill the hole with the last item
    heap.pop_back();
    if (pos < heap.size()) heap_update(heap, pos);
}
