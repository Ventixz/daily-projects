#include <algorithm>
#include <random>
#include <vector>

#include "heap.h"
#include "testing.h"

namespace {

void verify(const std::vector<HeapItem> &h) {
    for (size_t i = 0; i < h.size(); i++) {
        CHECK_EQ(h[i].node->heap_idx, i);  // back-references are exact
        if (i > 0) CHECK(h[(i - 1) / 2].val <= h[i].val);  // heap property
    }
}

}  // namespace

TEST(heap_set_update_remove) {
    std::vector<HeapItem> h;
    HeapNode a, b, c;
    heap_set(h, &a, 30);
    heap_set(h, &b, 10);
    heap_set(h, &c, 20);
    verify(h);
    CHECK(h[0].node == &b);
    heap_set(h, &a, 5);  // decrease
    CHECK(h[0].node == &a);
    heap_set(h, &a, 50);  // increase
    CHECK(h[0].node == &b);
    verify(h);
    heap_remove(h, &b);
    CHECK_EQ(b.heap_idx, k_not_in_heap);
    CHECK(h[0].node == &c);
    heap_remove(h, &b);  // no-op
    CHECK_EQ(h.size(), size_t(2));
    verify(h);
}

TEST(heap_random_ops_keep_invariants) {
    std::mt19937 rng(3);
    std::vector<HeapNode> nodes(500);
    std::vector<uint64_t> vals(nodes.size());
    std::vector<HeapItem> h;
    for (int step = 0; step < 50000; step++) {
        size_t i = rng() % nodes.size();
        if (rng() % 3) {
            vals[i] = rng() % 1000;
            heap_set(h, &nodes[i], vals[i]);
        } else {
            heap_remove(h, &nodes[i]);
        }
        if (step % 100 == 0) verify(h);
    }
    verify(h);
    for (const HeapItem &it : h) CHECK_EQ(it.val, vals[size_t(it.node - nodes.data())]);
    // Draining from the top yields sorted values.
    std::vector<uint64_t> drained;
    while (!h.empty()) {
        drained.push_back(h[0].val);
        heap_remove(h, h[0].node);
    }
    CHECK(std::is_sorted(drained.begin(), drained.end()));
}
