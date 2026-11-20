/* Unit tests for the data structures: hashtable, AVL tree, heap, sorted set. */
#include "../src/avl.h"
#include "../src/common.h"
#include "../src/hashtable.h"
#include "../src/heap.h"
#include "../src/zset.h"
#include "test.h"

#include <stdint.h>

/* ---- hashtable ---- */

typedef struct {
    HNode node;
    uint32_t key;
} IntNode;

static uint64_t int_hash(uint32_t k) { return str_hash((const uint8_t *)&k, sizeof k); }

static bool int_eq(const HNode *a, const HNode *b) {
    return container_of(a, IntNode, node)->key == container_of(b, IntNode, node)->key;
}

static IntNode *ht_find(HMap *m, uint32_t k) {
    IntNode probe = {.node = {.hcode = int_hash(k)}, .key = k};
    HNode *n = hm_lookup(m, &probe.node, int_eq);
    return n ? container_of(n, IntNode, node) : NULL;
}

static bool count_cb(HNode *n, void *arg) { (void)n; (*(size_t *)arg)++; return true; }

static void test_hashtable_insert_lookup_delete_across_rehash(void) {
    enum { N = 50000 };
    HMap m = {0};
    IntNode *nodes = calloc(N, sizeof *nodes);
    bool saw_rehash = false;
    for (uint32_t i = 0; i < N; i++) {
        nodes[i].key = i;
        nodes[i].node.hcode = int_hash(i);
        hm_insert(&m, &nodes[i].node);
        saw_rehash |= hm_rehashing(&m);
        /* Keys inserted before a resize must stay findable during it. */
        if (i % 997 == 0) CHECK(ht_find(&m, i / 2) == &nodes[i / 2]);
    }
    CHECK(saw_rehash);
    CHECK(hm_size(&m) == N);
    size_t seen = 0;
    hm_foreach(&m, count_cb, &seen);
    CHECK(seen == N);
    for (uint32_t i = 0; i < N; i++) CHECK(ht_find(&m, i) == &nodes[i]);
    CHECK(ht_find(&m, N + 1) == NULL);

    for (uint32_t i = 0; i < N; i += 2) {
        IntNode probe = {.node = {.hcode = int_hash(i)}, .key = i};
        CHECK(hm_delete(&m, &probe.node, int_eq) == &nodes[i].node);
    }
    CHECK(hm_size(&m) == N / 2);
    for (uint32_t i = 0; i < N; i++) CHECK((ht_find(&m, i) != NULL) == (i % 2 == 1));
    hm_clear(&m);
    free(nodes);
}

static void test_hashtable_resize_work_is_bounded(void) {
    /* With progressive rehashing, the old table drains a bounded amount per
     * operation, so right after a resize starts most nodes are still in it. */
    HMap m = {0};
    enum { N = 4 * 8 * 64 };  /* enough to trigger several resizes */
    IntNode *nodes = calloc(N, sizeof *nodes);
    size_t max_older = 0;
    for (uint32_t i = 0; i < N; i++) {
        nodes[i].key = i;
        nodes[i].node.hcode = int_hash(i);
        hm_insert(&m, &nodes[i].node);
        if (m.older.size > max_older) max_older = m.older.size;
    }
    CHECK(max_older > 128);  /* a resize was not done all at once */
    hm_clear(&m);
    free(nodes);
}

/* ---- AVL ---- */

typedef struct {
    AVLNode tree;
    int val;
} AVLInt;

static AVLNode *avl_insert_int(AVLNode *root, AVLInt *n) {
    AVLNode *parent = NULL, **from = &root;
    while (*from) {
        parent = *from;
        from = n->val < container_of(parent, AVLInt, tree)->val ? &parent->left : &parent->right;
    }
    avl_init(&n->tree);
    *from = &n->tree;
    n->tree.parent = parent;
    return avl_fix(&n->tree);
}

/* Checks every invariant; returns the number of nodes visited. Fills `out`
 * with the in-order values. */
static uint32_t avl_verify(const AVLNode *node, const AVLNode *parent, int *out, uint32_t *pos) {
    if (!node) return 0;
    CHECK(node->parent == parent);
    uint32_t l = avl_verify(node->left, node, out, pos);
    out[(*pos)++] = container_of(node, AVLInt, tree)->val;
    uint32_t r = avl_verify(node->right, node, out, pos);
    uint32_t hl = avl_height(node->left), hr = avl_height(node->right);
    CHECK(node->height == 1 + (hl > hr ? hl : hr));
    CHECK(hl <= hr + 1 && hr <= hl + 1);  /* balanced */
    CHECK(node->cnt == 1 + l + r);
    return node->cnt;
}

static uint32_t rng_state = 12345;
static uint32_t rng(void) { rng_state = rng_state * 1103515245u + 12345u; return rng_state >> 8; }

static void test_avl_random_insert_delete_keeps_invariants(void) {
    enum { N = 2000 };
    AVLInt *nodes = calloc(N, sizeof *nodes);
    bool *present = calloc(N, sizeof *present);
    int *inorder = calloc(N, sizeof *inorder);
    AVLNode *root = NULL;
    for (int i = 0; i < N; i++) nodes[i].val = i;
    for (int round = 0; round < 6000; round++) {
        int i = (int)(rng() % N);
        if (present[i]) root = avl_del(&nodes[i].tree);
        else root = avl_insert_int(root, &nodes[i]);
        present[i] = !present[i];
        if (round % 500 == 0 || round == 5999) {
            uint32_t pos = 0, expect = 0;
            avl_verify(root, NULL, inorder, &pos);
            for (int j = 0; j < N; j++) {
                if (!present[j]) continue;
                CHECK(inorder[expect] == j);  /* in-order walk is sorted */
                expect++;
            }
            CHECK(pos == expect);
            CHECK(avl_cnt(root) == expect);
        }
    }
    free(nodes); free(present); free(inorder);
}

static void test_avl_offset_and_rank(void) {
    enum { N = 300 };
    AVLInt *nodes = calloc(N, sizeof *nodes);
    AVLNode *root = NULL;
    for (int i = 0; i < N; i++) {
        nodes[i].val = (i * 7919) % N;  /* insert in scrambled order */
        root = avl_insert_int(root, &nodes[i]);
    }
    AVLInt *by_val[N];
    for (int i = 0; i < N; i++) by_val[nodes[i].val] = &nodes[i];
    for (int i = 0; i < N; i++) CHECK(avl_rank(&by_val[i]->tree) == i);
    for (int start = 0; start < N; start += 17) {
        for (int off = -start - 2; off < N - start + 2; off++) {
            AVLNode *got = avl_offset(&by_val[start]->tree, off);
            int target = start + off;
            if (target < 0 || target >= N) CHECK(got == NULL);
            else CHECK(got == &by_val[target]->tree);
        }
    }
    free(nodes);
}

/* ---- heap ---- */

static void test_heap_order_and_owner_indexes(void) {
    enum { N = 500 };
    Heap h = {0};
    size_t refs[N];
    uint64_t vals[N];
    for (int i = 0; i < N; i++) {
        refs[i] = HEAP_NONE;
        vals[i] = rng() % 100000;
        heap_upsert(&h, &refs[i], vals[i]);
    }
    /* Reschedule some, remove some. */
    for (int i = 0; i < N; i += 3) { vals[i] = rng() % 100000; heap_upsert(&h, &refs[i], vals[i]); }
    for (int i = 1; i < N; i += 5) { heap_remove(&h, &refs[i]); CHECK(refs[i] == HEAP_NONE); }
    /* Every owner's stored index must point at its own item. */
    for (int i = 0; i < N; i++) {
        if (refs[i] == HEAP_NONE) continue;
        CHECK(h.items[refs[i]].ref == &refs[i]);
        CHECK(h.items[refs[i]].val == vals[i]);
    }
    uint64_t prev = 0;
    size_t popped = 0;
    while (heap_top(&h)) {
        const HeapItem *top = heap_top(&h);
        CHECK(top->val >= prev);
        prev = top->val;
        heap_remove(&h, top->ref);
        popped++;
    }
    CHECK(popped == N - (N + 3) / 5);
    heap_free(&h);
}

/* ---- sorted set ---- */

static void test_zset_order_update_seek(void) {
    ZSet z = {0};
    CHECK(zset_insert(&z, "carol", 5, 3.0));
    CHECK(zset_insert(&z, "alice", 5, 1.0));
    CHECK(zset_insert(&z, "bob", 3, 2.0));
    CHECK(zset_insert(&z, "bobby", 5, 2.0));   /* same score: ordered by name */
    CHECK(!zset_insert(&z, "alice", 5, 4.0));  /* update moves it to the end */
    CHECK(zset_size(&z) == 4);

    const char *expect[] = {"bob", "bobby", "carol", "alice"};
    ZNode *n = zset_seekge(&z, -1e300, "", 0);
    for (int i = 0; i < 4; i++) {
        CHECK(n && n->len == strlen(expect[i]) && memcmp(n->name, expect[i], n->len) == 0);
        CHECK(n && znode_rank(n) == i);
        n = znode_offset(n, 1);
    }
    CHECK(n == NULL);

    ZNode *ge = zset_seekge(&z, 2.0, "bobb", 4);
    CHECK(ge && ge->len == 5 && memcmp(ge->name, "bobby", 5) == 0);
    CHECK(zset_seekge(&z, 99, "", 0) == NULL);

    ZNode *bob = zset_lookup(&z, "bob", 3);
    CHECK(bob && bob->score == 2.0);
    zset_delete(&z, bob);
    CHECK(zset_lookup(&z, "bob", 3) == NULL);
    CHECK(zset_size(&z) == 3);
    CHECK(hm_size(&z.hmap) == 3);
    zset_clear(&z);
}

static void test_zset_binary_names(void) {
    ZSet z = {0};
    CHECK(zset_insert(&z, "a\0b", 3, 1));
    CHECK(zset_insert(&z, "a\0c", 3, 1));
    CHECK(zset_insert(&z, "a", 1, 1));
    CHECK(zset_size(&z) == 3);
    ZNode *first = zset_seekge(&z, 1, "", 0);
    CHECK(first && first->len == 1);  /* a prefix sorts before longer names */
    zset_clear(&z);
}

int main(void) {
    RUN(test_hashtable_insert_lookup_delete_across_rehash);
    RUN(test_hashtable_resize_work_is_bounded);
    RUN(test_avl_random_insert_delete_keeps_invariants);
    RUN(test_avl_offset_and_rank);
    RUN(test_heap_order_and_owner_indexes);
    RUN(test_zset_order_update_seek);
    RUN(test_zset_binary_names);
    DONE();
}
