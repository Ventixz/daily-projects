// Unit tests for the data structures, the protocol and the command layer.
// None of these touch the network; time is passed in explicitly.
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "avl.h"
#include "check.h"
#include "hashtable.h"
#include "heap.h"
#include "protocol.h"
#include "store.h"
#include "thread_pool.h"
#include "zset.h"

// ------------------------------------------------------------------ hashtable

namespace {
struct IntNode : HNode {
    uint64_t val = 0;
};
uint64_t int_hash(uint64_t v) { return str_hash(&v, sizeof v); }
bool int_eq(HNode *a, HNode *b) {
    return static_cast<IntNode *>(a)->val == static_cast<IntNode *>(b)->val;
}
IntNode *hm_find(HMap &m, uint64_t v) {
    IntNode key;
    key.val = v;
    key.hcode = int_hash(v);
    return static_cast<IntNode *>(hm_lookup(&m, &key, &int_eq));
}
IntNode *hm_remove(HMap &m, uint64_t v) {
    IntNode key;
    key.val = v;
    key.hcode = int_hash(v);
    return static_cast<IntNode *>(hm_delete(&m, &key, &int_eq));
}
}  // namespace

TEST(hashtable_insert_lookup_delete) {
    HMap m;
    const uint64_t n = 100000;
    for (uint64_t i = 0; i < n; i++) {
        IntNode *node = new IntNode;
        node->val = i;
        node->hcode = int_hash(i);
        hm_insert(&m, node);
    }
    CHECK_EQ(hm_size(&m), size_t(n));
    bool all_found = true;
    for (uint64_t i = 0; i < n; i++) all_found &= hm_find(m, i) && hm_find(m, i)->val == i;
    CHECK(all_found);
    CHECK(hm_find(m, n + 7) == nullptr);
    for (uint64_t i = 0; i < n; i += 2) delete hm_remove(m, i);
    CHECK_EQ(hm_size(&m), size_t(n / 2));
    bool ok = true;
    for (uint64_t i = 0; i < n; i++) ok &= (hm_find(m, i) != nullptr) == (i % 2 == 1);
    CHECK(ok);
    CHECK(hm_remove(m, 0) == nullptr);
    size_t visited = 0;
    hm_foreach(&m, [&](HNode *node) {
        visited++;
        delete static_cast<IntNode *>(node);
        return true;
    });
    CHECK_EQ(visited, size_t(n / 2));
    hm_clear(&m);
    CHECK_EQ(hm_size(&m), size_t(0));
}

// The point of progressive rehashing: when the table grows, the old table is
// drained a bounded number of nodes per operation, never all at once.
TEST(hashtable_rehash_is_incremental) {
    HMap m;
    std::vector<IntNode *> nodes;
    uint64_t i = 0;
    auto add = [&] {
        IntNode *node = new IntNode;
        node->val = i;
        node->hcode = int_hash(i);
        i++;
        nodes.push_back(node);
        hm_insert(&m, node);
    };
    // Fill until a rehash starts with a big old table.
    while (!(hm_rehashing(&m) && m.older.size > 10 * k_rehashing_work)) add();
    size_t before = m.older.size;
    hm_find(m, 0);  // one operation
    CHECK_EQ(before - m.older.size, k_rehashing_work);
    // Every key is still findable mid-rehash, from either table.
    bool ok = true;
    for (uint64_t k = 0; k < i; k++) ok &= hm_find(m, k) != nullptr;
    CHECK(ok);
    // Enough further operations finish the move and free the old table.
    for (int k = 0; k < 1000 && hm_rehashing(&m); k++) hm_find(m, 1);
    CHECK(!hm_rehashing(&m));
    CHECK_EQ(hm_size(&m), nodes.size());
    hm_clear(&m);
    for (auto *n : nodes) delete n;
}

// ------------------------------------------------------------------------ AVL

namespace {
struct Data : AVLNode {
    uint32_t val = 0;
};

struct Tree {
    AVLNode *root = nullptr;
    void add(uint32_t v) {
        Data *d = new Data;
        d->val = v;
        AVLNode *parent = nullptr;
        AVLNode **from = &root;
        while (*from) {
            parent = *from;
            from = v < static_cast<Data *>(parent)->val ? &parent->left : &parent->right;
        }
        *from = d;
        d->parent = parent;
        root = avl_fix(d);
    }
    bool del(uint32_t v) {
        AVLNode *cur = root;
        while (cur && static_cast<Data *>(cur)->val != v) {
            cur = v < static_cast<Data *>(cur)->val ? cur->left : cur->right;
        }
        if (!cur) return false;
        root = avl_del(cur);
        delete static_cast<Data *>(cur);
        return true;
    }
    ~Tree() {
        while (root) {
            AVLNode *n = root;
            root = avl_del(n);
            delete static_cast<Data *>(n);
        }
    }
};

// Checks parent links, heights, counts, balance and ordering.
bool avl_verify(AVLNode *parent, AVLNode *node) {
    if (!node) return true;
    bool ok = node->parent == parent;
    ok &= avl_verify(node, node->left) && avl_verify(node, node->right);
    uint32_t l = avl_height(node->left), r = avl_height(node->right);
    ok &= node->height == 1 + std::max(l, r);
    ok &= (l > r ? l - r : r - l) <= 1;
    ok &= node->cnt == 1 + avl_cnt(node->left) + avl_cnt(node->right);
    uint32_t v = static_cast<Data *>(node)->val;
    if (node->left) ok &= static_cast<Data *>(node->left)->val <= v;
    if (node->right) ok &= static_cast<Data *>(node->right)->val >= v;
    return ok;
}

void extract(AVLNode *node, std::multiset<uint32_t> &out) {
    if (!node) return;
    extract(node->left, out);
    out.insert(static_cast<Data *>(node)->val);
    extract(node->right, out);
}

bool tree_matches(Tree &t, const std::multiset<uint32_t> &ref) {
    std::multiset<uint32_t> got;
    extract(t.root, got);
    return avl_verify(nullptr, t.root) && got == ref;
}
}  // namespace

TEST(avl_random_ops_match_multiset) {
    std::mt19937 rng(42);
    Tree t;
    std::multiset<uint32_t> ref;
    bool ok = true;
    for (int i = 0; i < 4000; i++) {
        uint32_t v = rng() % 500;
        if (rng() % 3) {
            t.add(v);
            ref.insert(v);
        } else {
            bool had = ref.count(v) > 0;
            ok &= t.del(v) == had;
            if (had) ref.erase(ref.find(v));
        }
        if (i % 97 == 0) ok &= tree_matches(t, ref);
    }
    CHECK(ok);
    CHECK(tree_matches(t, ref));
}

TEST(avl_sequential_inserts_stay_balanced) {
    Tree t;
    for (uint32_t i = 0; i < 1023; i++) t.add(i);  // worst case for a plain BST
    CHECK(avl_verify(nullptr, t.root));
    CHECK(avl_height(t.root) <= 11);  // 1023 = 2^10 - 1 nodes; AVL bound is ~1.44 log n
    CHECK_EQ(avl_cnt(t.root), 1023u);
}

TEST(avl_offset_and_rank) {
    for (uint32_t size = 1; size < 60; size++) {
        Tree t;
        for (uint32_t i = 0; i < size; i++) t.add(i);
        std::vector<AVLNode *> inorder;
        std::function<void(AVLNode *)> walk = [&](AVLNode *n) {
            if (!n) return;
            walk(n->left);
            inorder.push_back(n);
            walk(n->right);
        };
        walk(t.root);
        bool ok = true;
        for (uint32_t i = 0; i < size; i++) {
            ok &= avl_rank(inorder[i]) == int64_t(i);
            for (int64_t j = 0; j < int64_t(size); j++) {
                ok &= avl_offset(inorder[i], j - int64_t(i)) == inorder[j];
            }
            ok &= avl_offset(inorder[i], -int64_t(i) - 1) == nullptr;
            ok &= avl_offset(inorder[i], int64_t(size) - int64_t(i)) == nullptr;
        }
        CHECK(ok);
    }
}

// ----------------------------------------------------------------------- zset

namespace {
using Pair = std::pair<double, std::string>;
std::vector<Pair> zset_dump(ZSet &z) {
    std::vector<Pair> out;
    for (ZNode *n = zset_seekge(&z, -INFINITY, ""); n; n = znode_offset(n, 1)) {
        out.emplace_back(n->score, std::string(znode_name(n)));
    }
    return out;
}
}  // namespace

TEST(zset_orders_by_score_then_name) {
    ZSet z;
    CHECK(zset_insert(&z, "carol", 2));
    CHECK(zset_insert(&z, "bob", 1));
    CHECK(zset_insert(&z, "alice", 1));
    CHECK(zset_insert(&z, "dave", -5));
    std::vector<Pair> want = {{-5, "dave"}, {1, "alice"}, {1, "bob"}, {2, "carol"}};
    CHECK(zset_dump(z) == want);
    // Re-adding updates the score and moves the member.
    CHECK(!zset_insert(&z, "dave", 10));
    want = {{1, "alice"}, {1, "bob"}, {2, "carol"}, {10, "dave"}};
    CHECK(zset_dump(z) == want);
    CHECK_EQ(zset_lookup(&z, "dave")->score, 10.0);
    CHECK_EQ(znode_rank(zset_lookup(&z, "carol")), int64_t(2));
    // seekge lands on the first member >= the pair.
    CHECK_EQ(znode_name(zset_seekge(&z, 1, "b")), std::string_view("bob"));
    CHECK_EQ(znode_name(zset_seekge(&z, 1.5, "")), std::string_view("carol"));
    CHECK(zset_seekge(&z, 11, "") == nullptr);
    CHECK(zset_delete(&z, "bob"));
    CHECK(!zset_delete(&z, "bob"));
    CHECK(zset_lookup(&z, "bob") == nullptr);
    CHECK_EQ(zset_size(&z), size_t(3));
    zset_clear(&z);
    CHECK_EQ(zset_size(&z), size_t(0));
}

TEST(zset_random_ops_match_reference) {
    std::mt19937 rng(7);
    ZSet z;
    std::map<std::string, double> ref;
    bool ok = true;
    for (int i = 0; i < 5000; i++) {
        std::string name = "m" + std::to_string(rng() % 300);
        int op = rng() % 3;
        if (op < 2) {
            double score = double(rng() % 50) / 4;  // plenty of equal scores
            ok &= zset_insert(&z, name, score) == (ref.count(name) == 0);
            ref[name] = score;
        } else {
            ok &= zset_delete(&z, name) == (ref.erase(name) == 1);
        }
    }
    std::vector<Pair> want;
    for (auto &[n, s] : ref) want.emplace_back(s, n);
    std::sort(want.begin(), want.end());
    CHECK(ok);
    CHECK(zset_dump(z) == want);
    bool ranks = true;
    for (size_t i = 0; i < want.size(); i++) {
        ranks &= znode_rank(zset_lookup(&z, want[i].second)) == int64_t(i);
    }
    CHECK(ranks);
    zset_clear(&z);
}

TEST(zset_binary_safe_names) {
    ZSet z;
    std::string a("a\0b", 3), b("a\0c", 3);
    CHECK(zset_insert(&z, a, 1));
    CHECK(zset_insert(&z, b, 1));
    CHECK(zset_insert(&z, "", 1));
    CHECK_EQ(zset_size(&z), size_t(3));
    CHECK(zset_lookup(&z, a) != zset_lookup(&z, b));
    CHECK_EQ(znode_rank(zset_lookup(&z, "")), int64_t(0));
    zset_clear(&z);
}

// ----------------------------------------------------------------------- heap

TEST(heap_random_ops_keep_order_and_refs) {
    std::mt19937 rng(3);
    const size_t n = 500;
    std::vector<size_t> idx(n, k_heap_none);
    std::vector<uint64_t> val(n, 0);
    std::vector<HeapItem> heap;
    bool ok = true;
    for (int step = 0; step < 20000; step++) {
        size_t who = rng() % n;
        if (rng() % 4 == 0) {
            if (idx[who] != k_heap_none) heap_delete(heap, idx[who]);
            ok &= idx[who] == k_heap_none;
        } else {
            val[who] = rng() % 100000;
            HeapItem item;
            item.val = val[who];
            item.ref = &idx[who];
            heap_upsert(heap, idx[who], item);
        }
    }
    for (size_t i = 0; i < heap.size(); i++) {
        if (i > 0) ok &= heap[(i - 1) / 2].val <= heap[i].val;  // heap property
        size_t owner = size_t(heap[i].ref - idx.data());
        ok &= idx[owner] == i && val[owner] == heap[i].val;  // refs are exact
    }
    size_t live = 0;
    for (size_t i : idx) live += i != k_heap_none;
    ok &= live == heap.size();
    // Popping yields deadlines in order.
    uint64_t last = 0;
    while (!heap.empty()) {
        ok &= heap[0].val >= last;
        last = heap[0].val;
        heap_delete(heap, 0);
    }
    CHECK(ok);
}

// ------------------------------------------------------------------- protocol

TEST(protocol_request_roundtrip_and_partial) {
    std::vector<std::string> args = {"set", "key", std::string("\0bin\xff", 5), ""};
    Buffer buf;
    encode_request(buf, args);
    encode_request(buf, {"get", "key"});
    std::vector<std::string> got;
    size_t used = 0;
    // Every strict prefix of the first frame is incomplete, never an error.
    bool ok = true;
    size_t first_len = 4 + get_u32(buf.data());
    for (size_t n = 0; n < first_len; n++) ok &= parse_request(buf.data(), n, got, used) == Parse::Incomplete;
    CHECK(ok);
    CHECK(parse_request(buf.data(), buf.size(), got, used) == Parse::Ok);
    CHECK(got == args);
    CHECK_EQ(used, first_len);
    CHECK(parse_request(buf.data() + used, buf.size() - used, got, used) == Parse::Ok);
    CHECK((got == std::vector<std::string>{"get", "key"}));
}

TEST(protocol_rejects_malformed_requests) {
    std::vector<std::string> got;
    size_t used = 0;
    Buffer b;
    put_u32(b, uint32_t(k_max_msg + 1));  // too big: rejected before buffering it all
    CHECK(parse_request(b.data(), b.size(), got, used) == Parse::Error);

    b.clear();  // nstr says 2 but only 1 string present
    put_u32(b, 4 + 4 + 1);
    put_u32(b, 2);
    put_u32(b, 1);
    b.push_back('x');
    CHECK(parse_request(b.data(), b.size(), got, used) == Parse::Error);

    b.clear();  // string length runs past the frame
    put_u32(b, 4 + 4);
    put_u32(b, 1);
    put_u32(b, 100);
    CHECK(parse_request(b.data(), b.size(), got, used) == Parse::Error);

    b.clear();  // trailing bytes after the last string
    put_u32(b, 4 + 1);
    put_u32(b, 0);
    b.push_back('!');
    CHECK(parse_request(b.data(), b.size(), got, used) == Parse::Error);

    b.clear();  // too many arguments
    put_u32(b, 4);
    put_u32(b, uint32_t(k_max_args + 1));
    CHECK(parse_request(b.data(), b.size(), got, used) == Parse::Error);
}

TEST(protocol_response_roundtrip) {
    Buffer b;
    size_t h = response_begin(b);
    size_t ctx = out_begin_arr(b);
    out_nil(b);
    out_str(b, "hi");
    out_int(b, -42);
    out_dbl(b, 0.1);
    out_err(b, ERR_BAD_ARG, "nope");
    out_arr(b, 1);
    out_int(b, 7);
    out_end_arr(b, ctx, 6);
    response_end(b, h);
    Value v;
    size_t used = 0;
    CHECK(parse_response(b.data(), b.size(), v, used) == Parse::Ok);
    CHECK_EQ(used, b.size());
    CHECK(v.tag == TAG_ARR && v.arr.size() == 6);
    if (v.arr.size() == 6) {
        CHECK(v.arr[0].tag == TAG_NIL);
        CHECK_EQ(v.arr[1].s, std::string("hi"));
        CHECK_EQ(v.arr[2].i, int64_t(-42));
        CHECK_EQ(v.arr[3].d, 0.1);
        CHECK(v.arr[4].tag == TAG_ERR && v.arr[4].code == ERR_BAD_ARG && v.arr[4].s == "nope");
        CHECK(v.arr[5].arr.size() == 1 && v.arr[5].arr[0].i == 7);
    }
    CHECK_EQ(format_value(v.arr[3]), std::string("(dbl) 0.1\n"));
}

TEST(protocol_rejects_lying_response) {
    Buffer b;
    size_t h = response_begin(b);
    out_arr(b, 1000000);  // claims a million elements in a 5-byte body
    response_end(b, h);
    Value v;
    size_t used = 0;
    CHECK(parse_response(b.data(), b.size(), v, used) == Parse::Error);
    b.clear();
    h = response_begin(b);
    b.push_back(99);  // unknown tag
    response_end(b, h);
    CHECK(parse_response(b.data(), b.size(), v, used) == Parse::Error);
}

TEST(protocol_oversized_response_becomes_error) {
    Buffer b;
    size_t h = response_begin(b);
    out_str(b, std::string(k_max_msg, 'x'));  // 5 bytes over the limit with header
    response_end(b, h);
    Value v;
    size_t used = 0;
    CHECK(parse_response(b.data(), b.size(), v, used) == Parse::Ok);
    CHECK(v.tag == TAG_ERR && v.code == ERR_TOO_BIG);
}

// ---------------------------------------------------------------- store/cmds

namespace {
Value run(Store &s, std::vector<std::string> cmd, uint64_t now = 1000) {
    Buffer b;
    size_t h = response_begin(b);
    s.execute(cmd, b, now);
    response_end(b, h);
    Value v;
    size_t used = 0;
    if (parse_response(b.data(), b.size(), v, used) != Parse::Ok) v.tag = TAG_ERR, v.s = "BAD";
    return v;
}
}  // namespace

TEST(store_strings) {
    Store s(nullptr);
    CHECK(run(s, {"get", "k"}).tag == TAG_NIL);
    CHECK(run(s, {"set", "k", "v1"}).tag == TAG_NIL);
    CHECK_EQ(run(s, {"get", "k"}).s, std::string("v1"));
    CHECK(run(s, {"SET", "k", "v2"}).tag == TAG_NIL);  // command names are case-insensitive
    CHECK_EQ(run(s, {"GeT", "k"}).s, std::string("v2"));
    CHECK_EQ(run(s, {"dbsize"}).i, int64_t(1));
    CHECK_EQ(run(s, {"del", "k"}).i, int64_t(1));
    CHECK_EQ(run(s, {"del", "k"}).i, int64_t(0));
    CHECK_EQ(run(s, {"ping"}).s, std::string("PONG"));
    CHECK_EQ(run(s, {"ping", "hey"}).s, std::string("hey"));
}

TEST(store_errors) {
    Store s(nullptr);
    Value v = run(s, {"nope"});
    CHECK(v.tag == TAG_ERR && v.code == ERR_UNKNOWN);
    v = run(s, {"get"});
    CHECK(v.tag == TAG_ERR && v.code == ERR_BAD_ARG);
    v = run(s, {});
    CHECK(v.tag == TAG_ERR && v.code == ERR_UNKNOWN);
    run(s, {"set", "str", "x"});
    v = run(s, {"zadd", "str", "1", "m"});
    CHECK(v.tag == TAG_ERR && v.code == ERR_BAD_TYP);
    run(s, {"zadd", "z", "1", "m"});
    v = run(s, {"get", "z"});
    CHECK(v.tag == TAG_ERR && v.code == ERR_BAD_TYP);
    for (const char *bad : {"", "nan", "1x", "abc"}) {
        v = run(s, {"zadd", "z", bad, "m"});
        CHECK(v.tag == TAG_ERR && v.code == ERR_BAD_ARG);
    }
    v = run(s, {"pexpire", "z", "10.5"});
    CHECK(v.tag == TAG_ERR && v.code == ERR_BAD_ARG);
    v = run(s, {"zquery", "z", "0", "", "x", "1"});
    CHECK(v.tag == TAG_ERR && v.code == ERR_BAD_ARG);
}

TEST(store_keys_lists_everything) {
    Store s(nullptr);
    std::set<std::string> want;
    for (int i = 0; i < 50; i++) {
        want.insert("k" + std::to_string(i));
        run(s, {"set", "k" + std::to_string(i), "v"});
    }
    Value v = run(s, {"keys"});
    std::set<std::string> got;
    for (auto &e : v.arr) got.insert(e.s);
    CHECK(got == want);
}

TEST(store_ttl_with_injected_clock) {
    Store s(nullptr);
    run(s, {"set", "k", "v"}, 1000);
    CHECK_EQ(run(s, {"pttl", "k"}, 1000).i, int64_t(-1));
    CHECK_EQ(run(s, {"pttl", "missing"}, 1000).i, int64_t(-2));
    CHECK_EQ(run(s, {"pexpire", "k", "500"}, 1000).i, int64_t(1));
    CHECK_EQ(run(s, {"pexpire", "missing", "500"}, 1000).i, int64_t(0));
    CHECK_EQ(run(s, {"pttl", "k"}, 1200).i, int64_t(300));
    CHECK_EQ(s.next_deadline(), uint64_t(1500));
    CHECK_EQ(run(s, {"get", "k"}, 1499).s, std::string("v"));
    // Lazy expiry: at the deadline the key is gone even though expire() never ran.
    CHECK(run(s, {"get", "k"}, 1500).tag == TAG_NIL);
    CHECK_EQ(s.size(), size_t(0));
    CHECK_EQ(s.ttl_count(), size_t(0));

    // Negative TTL removes the deadline; SET also clears it.
    run(s, {"set", "a", "1"}, 0);
    run(s, {"pexpire", "a", "100"}, 0);
    run(s, {"pexpire", "a", "-1"}, 0);
    CHECK_EQ(run(s, {"pttl", "a"}, 0).i, int64_t(-1));
    run(s, {"pexpire", "a", "100"}, 0);
    run(s, {"set", "a", "2"}, 0);
    CHECK_EQ(run(s, {"pttl", "a"}, 0).i, int64_t(-1));
    CHECK_EQ(s.ttl_count(), size_t(0));
}

TEST(store_active_expiry_is_bounded) {
    Store s(nullptr);
    for (int i = 0; i < 5000; i++) {
        std::string k = "k" + std::to_string(i);
        run(s, {"set", k, "v"}, 0);
        run(s, {"pexpire", k, std::to_string(10 + i % 7)}, 0);
    }
    run(s, {"set", "forever", "v"}, 0);
    CHECK_EQ(s.expire(5), size_t(0));                       // nothing due yet
    CHECK_EQ(s.expire(100), k_max_expire_work);             // capped per call
    CHECK_EQ(s.expire(100), k_max_expire_work);
    CHECK_EQ(s.expire(100), size_t(5000 - 2 * k_max_expire_work));
    CHECK_EQ(s.size(), size_t(1));
    CHECK_EQ(s.next_deadline(), UINT64_MAX);
    // Keys due but not yet collected are hidden from KEYS and DBSIZE.
    run(s, {"set", "x", "v"}, 0);
    run(s, {"pexpire", "x", "1"}, 0);
    CHECK_EQ(run(s, {"dbsize"}, 10).i, int64_t(1));
    CHECK_EQ(run(s, {"keys"}, 10).arr.size(), size_t(1));
}

TEST(store_zset_commands) {
    Store s(nullptr);
    CHECK_EQ(run(s, {"zadd", "z", "1", "a"}).i, int64_t(1));
    CHECK_EQ(run(s, {"zadd", "z", "2", "b"}).i, int64_t(1));
    CHECK_EQ(run(s, {"zadd", "z", "3", "c"}).i, int64_t(1));
    CHECK_EQ(run(s, {"zadd", "z", "1.5", "c"}).i, int64_t(0));  // update
    CHECK_EQ(run(s, {"zscore", "z", "c"}).d, 1.5);
    CHECK(run(s, {"zscore", "z", "nobody"}).tag == TAG_NIL);
    CHECK(run(s, {"zscore", "nokey", "a"}).tag == TAG_NIL);
    CHECK_EQ(run(s, {"zrank", "z", "b"}).i, int64_t(2));
    Value v = run(s, {"zquery", "z", "1", "", "0", "10"});
    CHECK_EQ(v.arr.size(), size_t(6));
    if (v.arr.size() == 6) {
        CHECK(v.arr[0].s == "a" && v.arr[1].d == 1);
        CHECK(v.arr[2].s == "c" && v.arr[3].d == 1.5);
        CHECK(v.arr[4].s == "b" && v.arr[5].d == 2);
    }
    v = run(s, {"zquery", "z", "1.5", "c", "1", "1"});  // offset 1 past "c", one pair
    CHECK(v.arr.size() == 2 && v.arr[0].s == "b");
    v = run(s, {"zquery", "z", "2", "", "-1", "1"});  // negative offset steps back
    CHECK(v.arr.size() == 2 && v.arr[0].s == "c");
    CHECK_EQ(run(s, {"zquery", "z", "9", "", "0", "10"}).arr.size(), size_t(0));
    CHECK_EQ(run(s, {"zquery", "nokey", "0", "", "0", "10"}).arr.size(), size_t(0));
    CHECK_EQ(run(s, {"zquery", "z", "0", "", "0", "0"}).arr.size(), size_t(0));
    CHECK_EQ(run(s, {"zrem", "z", "a"}).i, int64_t(1));
    CHECK_EQ(run(s, {"zrem", "z", "a"}).i, int64_t(0));
    CHECK_EQ(run(s, {"zrem", "nokey", "a"}).i, int64_t(0));
    CHECK_EQ(run(s, {"set", "z", "now a string"}).tag, TAG_NIL);  // SET replaces a zset
    CHECK_EQ(run(s, {"get", "z"}).s, std::string("now a string"));
}

TEST(store_large_zset_freed_on_worker_thread) {
    ThreadPool pool(2);
    {
        Store s(&pool);
        for (int i = 0; i < 20000; i++) run(s, {"zadd", "big", std::to_string(i), "m" + std::to_string(i)});
        for (int i = 0; i < 10; i++) run(s, {"zadd", "small", std::to_string(i), "m"});
        CHECK_EQ(run(s, {"del", "big"}).i, int64_t(1));
        CHECK_EQ(run(s, {"del", "small"}).i, int64_t(1));
        CHECK_EQ(s.size(), size_t(0));
        // Same path through expiry.
        for (int i = 0; i < 5000; i++) run(s, {"zadd", "big2", std::to_string(i), "m" + std::to_string(i)}, 0);
        run(s, {"pexpire", "big2", "5"}, 0);
        CHECK_EQ(s.expire(10), size_t(1));
        pool.wait_idle();  // ASan reports a leak if the worker didn't free it
    }
}

int main() { return run_all_tests(); }
