#include "heap.h"

namespace {

size_t heap_parent(size_t i) { return (i + 1) / 2 - 1; }

void place(HeapItem *a, size_t pos, const HeapItem &item) {
    a[pos] = item;
    a[pos].node->heap_idx = pos;
}

void heap_up(HeapItem *a, size_t pos) {
    HeapItem t = a[pos];
    while (pos > 0 && a[heap_parent(pos)].val > t.val) {
        place(a, pos, a[heap_parent(pos)]);
        pos = heap_parent(pos);
    }
    place(a, pos, t);
}

void heap_down(HeapItem *a, size_t pos, size_t len) {
    HeapItem t = a[pos];
    while (true) {
        size_t l = pos * 2 + 1;
        size_t r = l + 1;
        size_t min_pos = pos;
        uint64_t min_val = t.val;
        if (l < len && a[l].val < min_val) {
            min_pos = l;
            min_val = a[l].val;
        }
        if (r < len && a[r].val < min_val) min_pos = r;
        if (min_pos == pos) break;
        place(a, pos, a[min_pos]);
        pos = min_pos;
    }
    place(a, pos, t);
}

void heap_update(HeapItem *a, size_t pos, size_t len) {
    if (pos > 0 && a[heap_parent(pos)].val > a[pos].val) {
        heap_up(a, pos);
    } else {
        heap_down(a, pos, len);
    }
}

}  // namespace

void heap_set(std::vector<HeapItem> &heap, HeapNode *node, uint64_t val) {
    if (node->heap_idx == k_not_in_heap) {
        heap.push_back(HeapItem{val, node});
        node->heap_idx = heap.size() - 1;
    } else {
        heap[node->heap_idx].val = val;
    }
    heap_update(heap.data(), node->heap_idx, heap.size());
}

void heap_remove(std::vector<HeapItem> &heap, HeapNode *node) {
    size_t pos = node->heap_idx;
    if (pos == k_not_in_heap) return;
    // Fill the hole with the last item and sift it whichever way it needs.
    heap[pos] = heap.back();
    heap.pop_back();
    if (pos < heap.size()) {
        heap[pos].node->heap_idx = pos;
        heap_update(heap.data(), pos, heap.size());
    }
    node->heap_idx = k_not_in_heap;
}
