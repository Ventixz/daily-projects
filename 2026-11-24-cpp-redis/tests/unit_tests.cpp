// Unit tests for the data structures and command layer. No framework: each
// test is a function, CHECK records failures, and main() runs them all.
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <map>
#include <random>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "avl.h"
#include "hashtable.h"
#include "heap.h"
#include "resp.h"
#include "store.h"
#include "thread_pool.h"
#include "zset.h"

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        g_checks++;                                                          \
        if (!(cond)) {                                                       \
            g_failures++;                                                    \
            fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                    \
    } while (0)

#define CHECK_EQ(a, b) CHECK((a) == (b))

// ---- hash table -------------------------------------------------------------

struct TestNode : HNode {
    uint64_t key = 0;
    uint64_t value = 0;
};

static bool node_eq(const HNode *a, const HNode *b) {
    return static_cast<const TestNode *>(a)->key == static_cast<const TestNode *>(b)->key;
}

static uint64_t int_hash(uint64_t k) { return str_hash(reinterpret_cast<const uint8_t *>(&k), sizeof(k)); }

static TestNode *hlookup(HMap *m, uint64_t key) {
    TestNode probe;
    probe.key = key;
    probe.hcode = int_hash(key);
    return static_cast<TestNode *>(hm_lookup(m, &probe, &node_eq));
}

static void test_hashtable_random_against_std() {
    HMap m;
    std::unordered_map<uint64_t, uint64_t> ref;
    std::mt19937_64 rng(42);
    bool saw_rehash = false;
    for (int i = 0; i < 200000; i++) {
        uint64_t key = rng() % 20000;
        int op = static_cast<int>(rng() % 3);
        if (op < 2) {  // insert or overwrite
            if (TestNode *n = hlookup(&m, key)) {
                n->value = i;
            } else {
                auto *node = new TestNode;
                node->key = key;
                node->value = i;
                node->hcode = int_hash(key);
                hm_insert(&m, node);
            }
            ref[key] = i;
        } else {
            TestNode probe;
            probe.key = key;
            probe.hcode = int_hash(key);
            HNode *gone = hm_delete(&m, &probe, &node_eq);
            CHECK_EQ(gone != nullptr, ref.erase(key) == 1);
            delete static_cast<TestNode *>(gone);
        }
        saw_rehash |= hm_rehashing(&m);
        if (i % 997 == 0) {
            // Mid-resize, lookups must still find keys in either table.
            uint64_t probe_key = rng() % 20000;
            TestNode *n = hlookup(&m, probe_key);
            auto it = ref.find(probe_key);
            CHECK_EQ(n != nullptr, it != ref.end());
            if (n && it != ref.end()) CHECK_EQ(n->value, it->second);
        }
    }
    CHECK(saw_rehash);
    CHECK_EQ(hm_size(&m), ref.size());
    size_t visited = 0;
    hm_foreach(
        &m,
        [](HNode *node, void *arg) {
            ++*static_cast<size_t *>(arg);
            delete static_cast<TestNode *>(node);
            return true;
        },
        &visited);
    CHECK_EQ(visited, ref.size());
    hm_clear(&m);
}

static void test_hashtable_rehash_is_incremental() {
    // Fill until a resize of a big table starts, then count how many more
    // operations it takes to finish: the work is spread over many of them.
    // (Small tables finish within a single step.)
    HMap m;
    std::vector<TestNode *> nodes;
    for (uint64_t k = 0; k < 10000 || !hm_rehashing(&m); k++) {
        auto *node = new TestNode;
        node->key = k;
        node->hcode = int_hash(k);
        hm_insert(&m, node);
        nodes.push_back(node);
    }
    size_t steps = 0;
    while (hm_rehashing(&m)) {
        hlookup(&m, 123456789);  // any operation helps the migration along
        steps++;
    }
    CHECK(steps > 10);
    CHECK(nodes.size() > 10000);
    for (uint64_t k = 0; k < nodes.size(); k++) CHECK(hlookup(&m, k) == nodes[k]);
    for (auto *n : nodes) delete n;
    hm_clear(&m);
}

// ---- AVL --------------------------------------------------------------------

struct IntNode : AVLNode {
    int val = 0;
};

static IntNode *as_int(AVLNode *n) { return static_cast<IntNode *>(n); }

static AVLNode *avl_insert_int(AVLNode *root, IntNode *node) {
    AVLNode *parent = nullptr;
    AVLNode **from = &root;
    while (*from) {
        parent = *from;
        from = node->val < as_int(parent)->val ? &parent->left : &parent->right;
    }
    *from = node;
    node->parent = parent;
    return avl_fix(node);
}

// Checks parent links, height, count, balance and ordering; returns false on
// the first violation.
static bool avl_verify(AVLNode *parent, AVLNode *node, std::vector<int> &inorder) {
    if (!node) return true;
    if (node->parent != parent) return false;
    if (!avl_verify(node, node->left, inorder)) return false;
    inorder.push_back(as_int(node)->val);
    if (!avl_verify(node, node->right, inorder)) return false;
    uint32_t l = avl_height(node->left), r = avl_height(node->right);
    if (node->height != 1 + std::max(l, r)) return false;
    if ((l > r ? l - r : r - l) > 1) return false;
    return node->cnt == 1 + avl_cnt(node->left) + avl_cnt(node->right);
}

static void test_avl_random_against_multiset() {
    std::mt19937 rng(7);
    AVLNode *root = nullptr;
    std::multiset<int> ref;
    std::vector<IntNode *> live;
    for (int i = 0; i < 4000; i++) {
        if (live.empty() || rng() % 3 != 0) {
            auto *n = new IntNode;
            n->val = static_cast<int>(rng() % 500);
            root = avl_insert_int(root, n);
            ref.insert(n->val);
            live.push_back(n);
        } else {
            size_t idx = rng() % live.size();
            IntNode *n = live[idx];
            live[idx] = live.back();
            live.pop_back();
            root = avl_del(n);
            ref.erase(ref.find(n->val));
            delete n;
        }
        if (i % 50 == 0 || i < 100) {
            std::vector<int> inorder;
            CHECK(avl_verify(nullptr, root, inorder));
            CHECK(std::equal(inorder.begin(), inorder.end(), ref.begin(), ref.end()));
        }
    }
    for (auto *n : live) delete n;
}

static void test_avl_offset_rank_at() {
    AVLNode *root = nullptr;
    std::vector<IntNode *> nodes;
    for (int i = 0; i < 300; i++) {
        auto *n = new IntNode;
        n->val = i;
        nodes.push_back(n);
        root = avl_insert_int(root, n);
    }
    for (int i = 0; i < 300; i += 7) {
        CHECK_EQ(avl_rank(nodes[i]), i);
        CHECK(avl_at(root, i) == nodes[i]);
        for (int j = 0; j < 300; j += 11) CHECK(avl_offset(nodes[i], j - i) == nodes[j]);
        CHECK(avl_offset(nodes[i], 300 - i) == nullptr);
        CHECK(avl_offset(nodes[i], -i - 1) == nullptr);
    }
    CHECK(avl_at(root, -1) == nullptr);
    CHECK(avl_at(root, 300) == nullptr);
    for (auto *n : nodes) delete n;
}

// ---- sorted set -------------------------------------------------------------

static void test_zset_ordering_and_updates() {
    ZSet z;
    CHECK(z.insert("b", 2));
    CHECK(z.insert("a", 2));  // same score: ordered by name
    CHECK(z.insert("c", 1));
    CHECK(!z.insert("c", 3));  // update moves it to the end
    CHECK_EQ(z.size(), 3u);
    CHECK_EQ(z.at(0)->name, "a");
    CHECK_EQ(z.at(1)->name, "b");
    CHECK_EQ(z.at(2)->name, "c");
    CHECK_EQ(ZSet::rank(z.lookup("c")), 2);
    CHECK_EQ(z.lookup("c")->score, 3.0);
    CHECK(z.seek_ge(2, "")->name == "a");
    CHECK(z.seek_ge(2, "aa")->name == "b");
    CHECK(z.seek_ge(3.5, "") == nullptr);
    z.remove(z.lookup("a"));
    CHECK(z.lookup("a") == nullptr);
    CHECK_EQ(z.at(0)->name, "b");
    CHECK_EQ(z.size(), 2u);
}

static void test_zset_random_against_std() {
    ZSet z;
    std::map<std::string, double> by_name;
    std::mt19937 rng(99);
    for (int i = 0; i < 20000; i++) {
        std::string name = "m" + std::to_string(rng() % 3000);
        if (rng() % 4 == 0) {
            if (ZNode *n = z.lookup(name)) z.remove(n);
            by_name.erase(name);
        } else {
            double score = static_cast<double>(rng() % 100);
            z.insert(name, score);
            by_name[name] = score;
        }
    }
    std::vector<std::pair<double, std::string>> sorted;
    for (auto &[n, s] : by_name) sorted.emplace_back(s, n);
    std::sort(sorted.begin(), sorted.end());
    CHECK_EQ(z.size(), sorted.size());
    bool all_match = true;
    ZNode *node = z.at(0);
    for (size_t i = 0; i < sorted.size(); i++, node = ZSet::offset(node, 1)) {
        if (!node || node->score != sorted[i].first || node->name != sorted[i].second) all_match = false;
    }
    CHECK(all_match);
    CHECK(node == nullptr);
}

// ---- heap -------------------------------------------------------------------

static void test_heap_tracks_indices() {
    struct Owner {
        size_t idx = kNoHeapIdx;
        uint64_t val = 0;
    };
    std::vector<HeapItem> heap;
    std::vector<Owner> owners(500);
    std::mt19937_64 rng(3);
    for (auto &o : owners) {
        o.val = rng() % 10000;
        heap_upsert(heap, &o.idx, &o, o.val);
    }
    for (int i = 0; i < 200; i++) {  // change some, remove some
        Owner &o = owners[rng() % owners.size()];
        if (i % 2) {
            o.val = rng() % 10000;
            heap_upsert(heap, &o.idx, &o, o.val);
        } else {
            heap_remove(heap, &o.idx);
        }
    }
    bool ok = true;
    for (auto &o : owners) {
        if (o.idx != kNoHeapIdx && (heap[o.idx].ref != &o.idx || heap[o.idx].val != o.val)) ok = false;
    }
    CHECK(ok);
    uint64_t prev = 0;
    bool sorted = true;
    while (!heap.empty()) {
        if (heap[0].val < prev) sorted = false;
        prev = heap[0].val;
        heap_remove(heap, heap[0].ref);
    }
    CHECK(sorted);
}

// ---- RESP parser ------------------------------------------------------------

static void test_resp_parse_multibulk_and_inline() {
    std::vector<std::string> args;
    size_t used = 0;
    std::string err;
    std::string req = "*3\r\n$3\r\nSET\r\n$1\r\nk\r\n$5\r\nhe\r\no\r\n";  // binary-safe value
    CHECK(parse_request(req, args, &used, &err) == ParseResult::kOk);
    CHECK_EQ(used, req.size());
    CHECK((args == std::vector<std::string>{"SET", "k", "he\r\no"}));

    // Every proper prefix is incomplete, never an error.
    bool all_incomplete = true;
    for (size_t i = 0; i < req.size(); i++) {
        if (parse_request(std::string_view(req).substr(0, i), args, &used, &err) != ParseResult::kIncomplete) {
            all_incomplete = false;
        }
    }
    CHECK(all_incomplete);

    std::string inl = "  GET   key \r\nPING\r\n";
    CHECK(parse_request(inl, args, &used, &err) == ParseResult::kOk);
    CHECK((args == std::vector<std::string>{"GET", "key"}));
    CHECK(parse_request(std::string_view(inl).substr(used), args, &used, &err) == ParseResult::kOk);
    CHECK((args == std::vector<std::string>{"PING"}));
}

static void test_resp_parse_errors() {
    std::vector<std::string> args;
    size_t used = 0;
    std::string err;
    CHECK(parse_request("*x\r\n", args, &used, &err) == ParseResult::kError);
    CHECK(parse_request("*1\r\n:1\r\n", args, &used, &err) == ParseResult::kError);
    CHECK(parse_request("*1\r\n$-5\r\n", args, &used, &err) == ParseResult::kError);
    CHECK(parse_request("*1\r\n$3\r\nabcXY", args, &used, &err) == ParseResult::kError);
    CHECK(parse_request("*1\r\n$999999999999\r\n", args, &used, &err) == ParseResult::kError);
    CHECK(parse_request("*99999999\r\n", args, &used, &err) == ParseResult::kError);
    CHECK(parse_request("*1" + std::string(100, '1'), args, &used, &err) == ParseResult::kError);
}

static void test_format_double() {
    CHECK_EQ(format_double(1.5), "1.5");
    CHECK_EQ(format_double(0.1), "0.1");
    CHECK_EQ(format_double(3), "3");
    CHECK_EQ(format_double(-2.25), "-2.25");
    CHECK_EQ(format_double(1.0 / 0.0), "inf");
    CHECK_EQ(format_double(-1.0 / 0.0), "-inf");
}

// ---- commands -------------------------------------------------------------

static std::string run(Store &s, std::vector<std::string> args, uint64_t now = 1000) {
    std::string out;
    bool close_after = false;
    s.execute(args, out, now, &close_after);
    return out;
}

static void test_commands_strings_and_ttl() {
    Store s;
    CHECK_EQ(run(s, {"PING"}), "+PONG\r\n");
    CHECK_EQ(run(s, {"set", "k", "v"}), "+OK\r\n");
    CHECK_EQ(run(s, {"GET", "k"}), "$1\r\nv\r\n");
    CHECK_EQ(run(s, {"GET", "nope"}), "$-1\r\n");
    CHECK_EQ(run(s, {"SET", "k", "v2", "NX"}), "$-1\r\n");
    CHECK_EQ(run(s, {"SET", "new", "x", "XX"}), "$-1\r\n");
    CHECK_EQ(run(s, {"SET", "t", "x", "PX", "500"}, 1000), "+OK\r\n");
    CHECK_EQ(run(s, {"PTTL", "t"}, 1100), ":400\r\n");
    CHECK_EQ(run(s, {"TTL", "t"}, 1100), ":0\r\n");
    CHECK_EQ(run(s, {"TTL", "k"}), ":-1\r\n");
    CHECK_EQ(run(s, {"TTL", "missing"}), ":-2\r\n");
    CHECK_EQ(run(s, {"GET", "t"}, 1499), "$1\r\nx\r\n");
    CHECK_EQ(run(s, {"GET", "t"}, 1500), "$-1\r\n");  // lazily expired on access
    CHECK_EQ(s.ttl_count(), 0u);
    CHECK_EQ(run(s, {"EXPIRE", "k", "10"}), ":1\r\n");
    CHECK_EQ(run(s, {"PERSIST", "k"}), ":1\r\n");
    CHECK_EQ(run(s, {"PERSIST", "k"}), ":0\r\n");
    CHECK_EQ(run(s, {"EXPIRE", "k", "-1"}), ":1\r\n");  // past TTL deletes
    CHECK_EQ(run(s, {"EXISTS", "k"}), ":0\r\n");
    CHECK_EQ(run(s, {"SET", "k", "v", "EX", "0"}), "-ERR invalid expire time in 'set' command\r\n");
    CHECK_EQ(run(s, {"SET", "k", "v", "BOGUS"}), "-ERR syntax error\r\n");
    CHECK_EQ(run(s, {"GET"}), "-ERR wrong number of arguments for 'get' command\r\n");
    CHECK_EQ(run(s, {"NOPE", "a"}), "-ERR unknown command 'NOPE', with args beginning with: 'a' \r\n");
}

static void test_active_expiry() {
    Store s;
    for (int i = 0; i < 100; i++) run(s, {"SET", "k" + std::to_string(i), "v", "PX", std::to_string(100 + i)}, 0);
    run(s, {"SET", "keep", "v"}, 0);
    CHECK_EQ(s.size(), 101u);
    CHECK_EQ(s.next_expiry(), 100u);
    CHECK_EQ(s.expire_keys(149), 50u);
    CHECK_EQ(s.size(), 51u);
    CHECK_EQ(s.expire_keys(1000, 10), 10u);  // work per call is capped
    CHECK_EQ(s.expire_keys(1000), 40u);
    CHECK_EQ(s.size(), 1u);
    CHECK_EQ(s.next_expiry(), UINT64_MAX);
}

static void test_commands_zset() {
    Store s;
    CHECK_EQ(run(s, {"ZADD", "z", "1", "a", "2", "b", "3", "c"}), ":3\r\n");
    CHECK_EQ(run(s, {"ZADD", "z", "5", "a"}), ":0\r\n");
    CHECK_EQ(run(s, {"ZRANGE", "z", "0", "-1"}), "*3\r\n$1\r\nb\r\n$1\r\nc\r\n$1\r\na\r\n");
    CHECK_EQ(run(s, {"ZRANGE", "z", "-2", "-1", "WITHSCORES"}), "*4\r\n$1\r\nc\r\n$1\r\n3\r\n$1\r\na\r\n$1\r\n5\r\n");
    CHECK_EQ(run(s, {"ZRANK", "z", "a"}), ":2\r\n");
    CHECK_EQ(run(s, {"ZSCORE", "z", "b"}), "$1\r\n2\r\n");
    CHECK_EQ(run(s, {"ZINCRBY", "z", "0.5", "b"}), "$3\r\n2.5\r\n");
    CHECK_EQ(run(s, {"ZRANGEBYSCORE", "z", "(2.5", "+inf"}), "*2\r\n$1\r\nc\r\n$1\r\na\r\n");
    CHECK_EQ(run(s, {"ZRANGEBYSCORE", "z", "-inf", "+inf", "LIMIT", "1", "1"}), "*1\r\n$1\r\nc\r\n");
    CHECK_EQ(run(s, {"ZQUERY", "z", "3", "", "0", "10"}), "*4\r\n$1\r\nc\r\n$1\r\n3\r\n$1\r\na\r\n$1\r\n5\r\n");
    CHECK_EQ(run(s, {"ZQUERY", "z", "3", "", "-1", "1"}), "*2\r\n$1\r\nb\r\n$3\r\n2.5\r\n");
    CHECK_EQ(run(s, {"ZADD", "z", "nan", "x"}), "-ERR value is not a valid float\r\n");
    CHECK_EQ(run(s, {"ZADD", "z", "1", "x", "2"}), "-ERR syntax error\r\n");
    CHECK_EQ(run(s, {"GET", "z"}), "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n");
    CHECK_EQ(run(s, {"TYPE", "z"}), "+zset\r\n");
    CHECK_EQ(run(s, {"ZREM", "z", "a", "b", "c", "zzz"}), ":3\r\n");
    CHECK_EQ(run(s, {"EXISTS", "z"}), ":0\r\n");  // emptied set is deleted
}

static void test_keys_glob() {
    CHECK(glob_match("*", "anything"));
    CHECK(glob_match("h?llo", "hello"));
    CHECK(!glob_match("h?llo", "hllo"));
    CHECK(glob_match("h*llo", "heeeello"));
    CHECK(glob_match("h[ae]llo", "hallo"));
    CHECK(!glob_match("h[ae]llo", "hillo"));
    CHECK(glob_match("h[^e]llo", "hallo"));
    CHECK(!glob_match("h[^e]llo", "hello"));
    CHECK(glob_match("h[a-b]llo", "hbllo"));
    CHECK(glob_match("a\\*b", "a*b"));
    CHECK(!glob_match("a\\*b", "axb"));
    CHECK(glob_match("*a*b*c*", "xxaxxbxxcxx"));
    CHECK(!glob_match("*a*b*c*", "xxaxxcxxbxx"));
    Store s;
    run(s, {"SET", "user:1", "a"});
    run(s, {"SET", "user:2", "b"});
    run(s, {"SET", "other", "c"});
    std::string out = run(s, {"KEYS", "user:*"});
    CHECK(out.rfind("*2\r\n", 0) == 0);
}

static void test_large_zset_freed_on_worker() {
    ThreadPool pool(2);
    Store s(&pool);
    std::vector<std::string> args = {"ZADD", "big"};
    for (size_t i = 0; i < kLargeContainerSize + 10; i++) {
        args.push_back(std::to_string(i));
        args.push_back("m" + std::to_string(i));
    }
    CHECK_EQ(run(s, args), ":" + std::to_string(kLargeContainerSize + 10) + "\r\n");
    CHECK_EQ(run(s, {"DEL", "big"}), ":1\r\n");
    CHECK_EQ(run(s, {"EXISTS", "big"}), ":0\r\n");
    pool.wait_idle();
}

static void test_thread_pool_runs_everything() {
    std::atomic<int> n{0};
    {
        ThreadPool pool(4);
        for (int i = 0; i < 1000; i++) pool.submit([&n] { n++; });
        pool.wait_idle();
        CHECK_EQ(n.load(), 1000);
        for (int i = 0; i < 100; i++) pool.submit([&n] { n++; });
    }  // destructor drains the queue
    CHECK_EQ(n.load(), 1100);
}

int main() {
    struct {
        const char *name;
        void (*fn)();
    } tests[] = {
        {"hashtable_random_against_std", test_hashtable_random_against_std},
        {"hashtable_rehash_is_incremental", test_hashtable_rehash_is_incremental},
        {"avl_random_against_multiset", test_avl_random_against_multiset},
        {"avl_offset_rank_at", test_avl_offset_rank_at},
        {"zset_ordering_and_updates", test_zset_ordering_and_updates},
        {"zset_random_against_std", test_zset_random_against_std},
        {"heap_tracks_indices", test_heap_tracks_indices},
        {"resp_parse_multibulk_and_inline", test_resp_parse_multibulk_and_inline},
        {"resp_parse_errors", test_resp_parse_errors},
        {"format_double", test_format_double},
        {"commands_strings_and_ttl", test_commands_strings_and_ttl},
        {"active_expiry", test_active_expiry},
        {"commands_zset", test_commands_zset},
        {"keys_glob", test_keys_glob},
        {"large_zset_freed_on_worker", test_large_zset_freed_on_worker},
        {"thread_pool_runs_everything", test_thread_pool_runs_everything},
    };
    for (auto &t : tests) {
        int before = g_failures;
        t.fn();
        printf("%s %s\n", g_failures == before ? "ok  " : "FAIL", t.name);
    }
    printf("\n%zu tests, %d checks, %d failures\n", sizeof(tests) / sizeof(tests[0]), g_checks, g_failures);
    return g_failures ? 1 : 0;
}
