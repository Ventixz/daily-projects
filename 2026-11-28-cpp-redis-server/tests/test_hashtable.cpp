#include <memory>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

#include "hashtable.h"
#include "testing.h"

namespace {

struct Item : HNode {
    std::string key;
    explicit Item(std::string k) : key(std::move(k)) { hcode = hash_bytes(key.data(), key.size()); }
};

struct Probe : HNode {
    std::string_view key;
    explicit Probe(std::string_view k) : key(k) { hcode = hash_bytes(k.data(), k.size()); }
};

bool item_eq(const HNode *stored, const HNode *key) {
    return static_cast<const Item *>(stored)->key == static_cast<const Probe *>(key)->key;
}

size_t count_nodes(const HMap &m) {
    size_t n = 0;
    m.for_each([&](HNode *) {
        n++;
        return true;
    });
    return n;
}

}  // namespace

TEST(hash_fnv1a_known_values) {
    // Published FNV-1a 64-bit test vectors.
    CHECK_EQ(hash_bytes("", 0), 0xcbf29ce484222325ull);
    CHECK_EQ(hash_bytes("a", 1), 0xaf63dc4c8601ec8cull);
    CHECK_EQ(hash_bytes("foobar", 6), 0x85944171f73967e8ull);
}

TEST(hmap_insert_lookup_remove) {
    HMap m;
    Item a("alpha"), b("beta");
    m.insert(&a);
    m.insert(&b);
    CHECK_EQ(m.size(), size_t(2));
    Probe pa("alpha"), pc("gamma");
    CHECK(m.lookup(&pa, item_eq) == &a);
    CHECK(m.lookup(&pc, item_eq) == nullptr);
    CHECK(m.remove(&pa, item_eq) == &a);
    CHECK(m.lookup(&pa, item_eq) == nullptr);
    CHECK(m.remove(&pa, item_eq) == nullptr);
    CHECK_EQ(m.size(), size_t(1));
}

// The resize must be spread out: right after it triggers, both tables hold
// keys, every key is still findable, and iteration still sees all of them.
TEST(hmap_rehashing_is_progressive) {
    HMap m;
    std::vector<std::unique_ptr<Item>> items;
    bool saw_rehash = false;
    for (int i = 0; i < 5000; i++) {
        items.push_back(std::make_unique<Item>("k" + std::to_string(i)));
        m.insert(items.back().get());
        if (m.is_rehashing()) {
            saw_rehash = true;
            CHECK_EQ(count_nodes(m), m.size());
            Probe first("k0");
            CHECK(m.lookup(&first, item_eq) == items[0].get());
        }
    }
    CHECK(saw_rehash);
    // Lookups do migration work too, so enough of them finish the resize.
    for (int i = 0; i < 5000 && m.is_rehashing(); i++) {
        std::string key = "k" + std::to_string(i);  // Probe holds a view: keep it alive
        Probe p(key);
        m.lookup(&p, item_eq);
    }
    CHECK(!m.is_rehashing());
    CHECK(m.size() <= m.capacity() * 8);
}

// Random workload checked step by step against std::unordered_set.
TEST(hmap_matches_reference_under_random_ops) {
    HMap m;
    std::unordered_set<std::string> ref;
    std::vector<std::unique_ptr<Item>> pool(20000);
    std::mt19937 rng(12345);
    for (int step = 0; step < 200000; step++) {
        int id = int(rng() % pool.size());
        std::string key = "key:" + std::to_string(id);
        Probe p(key);
        int op = int(rng() % 3);
        if (op == 0 && !ref.count(key)) {
            pool[id] = std::make_unique<Item>(key);
            m.insert(pool[id].get());
            ref.insert(key);
        } else if (op == 1) {
            HNode *n = m.remove(&p, item_eq);
            CHECK_EQ(n != nullptr, ref.erase(key) == 1);
            if (n) CHECK(n == pool[id].get());
        } else {
            HNode *n = m.lookup(&p, item_eq);
            CHECK_EQ(n != nullptr, ref.count(key) == 1);
        }
        CHECK_EQ(m.size(), ref.size());
    }
    CHECK_EQ(count_nodes(m), ref.size());
}
