#include <iterator>
#include <random>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>

#include "testing.h"
#include "zset.h"

namespace {

using Ref = std::set<std::pair<double, std::string>>;

void check_same(ZSet &z, const Ref &ref) {
    CHECK_EQ(z.size(), ref.size());
    ZNode *n = z.seek_ge(-1e300, "");
    for (const auto &[score, name] : ref) {
        CHECK(n != nullptr);
        CHECK_EQ(n->score, score);
        CHECK_EQ(n->name, name);
        n = ZSet::offset(n, 1);
    }
    CHECK(n == nullptr);
}

}  // namespace

TEST(zset_insert_update_remove) {
    ZSet z;
    CHECK(z.insert("bob", 2));
    CHECK(z.insert("alice", 1));
    CHECK(!z.insert("bob", 3));  // update, not add
    CHECK_EQ(z.lookup("bob")->score, 3.0);
    CHECK_EQ(z.size(), size_t(2));
    CHECK(z.remove("alice"));
    CHECK(!z.remove("alice"));
    CHECK(z.lookup("alice") == nullptr);
    CHECK_EQ(z.size(), size_t(1));
}

TEST(zset_orders_by_score_then_name) {
    ZSet z;
    z.insert("c", 1);
    z.insert("a", 1);
    z.insert("b", 0.5);
    z.insert("d", -3);
    Ref ref{{1, "c"}, {1, "a"}, {0.5, "b"}, {-3, "d"}};
    check_same(z, ref);
    CHECK_EQ(ZSet::rank(z.lookup("a")), int64_t(2));
}

TEST(zset_seek_ge_and_offset) {
    ZSet z;
    for (int i = 0; i < 10; i++) z.insert("m" + std::to_string(i), i * 10);
    CHECK_EQ(z.seek_ge(25, "")->name, std::string("m3"));
    CHECK_EQ(z.seek_ge(30, "")->name, std::string("m3"));
    CHECK_EQ(z.seek_ge(30, "m3~")->name, std::string("m4"));  // same score, larger name
    CHECK(z.seek_ge(91, "") == nullptr);
    ZNode *m3 = z.seek_ge(30, "");
    CHECK_EQ(ZSet::offset(m3, -3)->name, std::string("m0"));
    CHECK_EQ(ZSet::offset(m3, 6)->name, std::string("m9"));
    CHECK(ZSet::offset(m3, 7) == nullptr);
    CHECK(ZSet::offset(nullptr, 1) == nullptr);
}

TEST(zset_matches_reference_under_random_ops) {
    ZSet z;
    Ref ref;
    std::unordered_map<std::string, double> scores;
    std::mt19937 rng(99);
    for (int step = 0; step < 20000; step++) {
        std::string name = "n" + std::to_string(rng() % 500);
        double score = double(rng() % 50);  // many ties, so name order matters
        if (rng() % 3) {
            auto it = scores.find(name);
            bool fresh = it == scores.end();
            if (!fresh) ref.erase({it->second, name});
            CHECK_EQ(z.insert(name, score), fresh);
            ref.insert({score, name});
            scores[name] = score;
        } else {
            auto it = scores.find(name);
            CHECK_EQ(z.remove(name), it != scores.end());
            if (it != scores.end()) {
                ref.erase({it->second, name});
                scores.erase(it);
            }
        }
        if (step % 500 == 0) check_same(z, ref);
    }
    check_same(z, ref);
    int64_t i = 0;
    for (const auto &[score, name] : ref) CHECK_EQ(ZSet::rank(z.lookup(name)), i++);
}
