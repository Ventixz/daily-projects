// Commands run directly against the Store with a fake clock, so TTL behaviour
// is tested exactly instead of with sleeps.
#include <cmath>
#include <string>
#include <vector>

#include "protocol.h"
#include "store.h"
#include "testing.h"

namespace {

struct Fixture {
    Store store;
    uint64_t now = 1000000;

    explicit Fixture(ThreadPool *pool = nullptr) : store(pool) {}

    Value run(std::vector<std::string> cmd) {
        std::string out;
        store.execute(cmd, out, now);
        Value v;
        if (!decode_response(reinterpret_cast<const uint8_t *>(out.data()), out.size(), v)) {
            throw testing::Failure{"store produced an undecodable reply"};
        }
        return v;
    }
    std::string text(std::vector<std::string> cmd) { return format_value(run(std::move(cmd))); }
};

}  // namespace

TEST(store_string_commands) {
    Fixture f;
    CHECK_EQ(f.text({"get", "k"}), std::string("(nil)\n"));
    CHECK_EQ(f.text({"set", "k", "v1"}), std::string("(nil)\n"));
    CHECK_EQ(f.text({"get", "k"}), std::string("(str) v1\n"));
    f.run({"set", "k", "v2"});
    CHECK_EQ(f.text({"get", "k"}), std::string("(str) v2\n"));
    CHECK_EQ(f.text({"del", "k"}), std::string("(int) 1\n"));
    CHECK_EQ(f.text({"del", "k"}), std::string("(int) 0\n"));
    CHECK_EQ(f.text({"ping"}), std::string("(str) PONG\n"));
    CHECK_EQ(f.text({"PING", "hey"}), std::string("(str) hey\n"));  // case-insensitive
}

TEST(store_argument_and_command_errors) {
    Fixture f;
    CHECK_EQ(f.run({"nope"}).code, uint32_t(ERR_UNKNOWN));
    CHECK_EQ(f.run({}).code, uint32_t(ERR_UNKNOWN));
    CHECK_EQ(f.run({"get"}).code, uint32_t(ERR_BAD_ARG));
    CHECK_EQ(f.run({"set", "a", "b", "c"}).code, uint32_t(ERR_BAD_ARG));
    f.run({"set", "k", "v"});
    CHECK_EQ(f.run({"pexpire", "k", "10x"}).code, uint32_t(ERR_BAD_ARG));
    CHECK_EQ(f.run({"pexpire", "k", " 10"}).code, uint32_t(ERR_BAD_ARG));
    CHECK_EQ(f.run({"pexpire", "k", "99999999999999999999"}).code, uint32_t(ERR_BAD_ARG));
    CHECK_EQ(f.run({"zadd", "z", "nan", "m"}).code, uint32_t(ERR_BAD_ARG));
    CHECK_EQ(f.run({"zadd", "z", "", "m"}).code, uint32_t(ERR_BAD_ARG));
    CHECK_EQ(f.run({"zquery", "z", "0", "", "x", "1"}).code, uint32_t(ERR_BAD_ARG));
}

TEST(store_type_errors_and_set_overwrites) {
    Fixture f;
    f.run({"set", "s", "v"});
    f.run({"zadd", "z", "1", "m"});
    CHECK_EQ(f.run({"zadd", "s", "1", "m"}).code, uint32_t(ERR_BAD_TYP));
    CHECK_EQ(f.run({"zscore", "s", "m"}).code, uint32_t(ERR_BAD_TYP));
    CHECK_EQ(f.run({"get", "z"}).code, uint32_t(ERR_BAD_TYP));
    f.run({"set", "z", "now a string"});  // SET replaces any type
    CHECK_EQ(f.text({"get", "z"}), std::string("(str) now a string\n"));
}

TEST(store_ttl_with_fake_clock) {
    Fixture f;
    f.run({"set", "k", "v"});
    CHECK_EQ(f.run({"pttl", "k"}).i, int64_t(-1));
    CHECK_EQ(f.run({"pttl", "missing"}).i, int64_t(-2));
    CHECK_EQ(f.run({"pexpire", "missing", "100"}).i, int64_t(0));
    CHECK_EQ(f.run({"pexpire", "k", "100"}).i, int64_t(1));
    CHECK_EQ(f.store.next_expiry(), int64_t(f.now + 100));
    f.now += 40;
    CHECK_EQ(f.run({"pttl", "k"}).i, int64_t(60));
    f.now += 59;
    CHECK_EQ(f.text({"get", "k"}), std::string("(str) v\n"));  // 1 ms left
    f.now += 1;
    // The timer has not run, but lookups refuse expired keys anyway.
    CHECK_EQ(f.store.size(), size_t(1));
    CHECK_EQ(f.text({"get", "k"}), std::string("(nil)\n"));
    CHECK_EQ(f.store.size(), size_t(0));
    CHECK_EQ(f.store.ttl_count(), size_t(0));
}

TEST(store_active_expiry_respects_work_limit) {
    Fixture f;
    for (int i = 0; i < 10; i++) {
        std::string k = "k" + std::to_string(i);
        f.run({"set", k, "v"});
        f.run({"pexpire", k, std::to_string(10 + i)});
    }
    f.run({"set", "forever", "v"});
    f.now += 14;  // k0..k4 are due
    CHECK_EQ(f.store.expire(f.now, 3), size_t(3));
    CHECK_EQ(f.store.expire(f.now, 100), size_t(2));
    CHECK_EQ(f.store.expire(f.now, 100), size_t(0));
    CHECK_EQ(f.run({"dbsize"}).i, int64_t(6));
    f.now += 1000;
    f.store.expire(f.now, 100);
    CHECK_EQ(f.text({"keys"}), std::string("(arr) len=1\n(str) forever\n(arr) end\n"));
}

TEST(store_set_persist_and_nonpositive_pexpire) {
    Fixture f;
    f.run({"set", "a", "1"});
    f.run({"pexpire", "a", "50"});
    f.run({"set", "a", "2"});  // SET clears the TTL
    CHECK_EQ(f.run({"pttl", "a"}).i, int64_t(-1));
    f.run({"pexpire", "a", "50"});
    CHECK_EQ(f.run({"persist", "a"}).i, int64_t(1));
    CHECK_EQ(f.run({"persist", "a"}).i, int64_t(0));
    f.now += 100;
    CHECK_EQ(f.text({"get", "a"}), std::string("(str) 2\n"));
    CHECK_EQ(f.run({"pexpire", "a", "0"}).i, int64_t(1));  // deletes now
    CHECK_EQ(f.run({"pttl", "a"}).i, int64_t(-2));
    f.run({"set", "b", "1"});
    f.run({"pexpire", "b", "9223372036854775807"});  // clamped, no overflow
    CHECK(f.run({"pttl", "b"}).i > 0);
}

TEST(store_keys_skips_expired_entries) {
    Fixture f;
    f.run({"set", "a", "1"});
    f.run({"set", "b", "1"});
    f.run({"pexpire", "b", "5"});
    f.now += 5;
    CHECK_EQ(f.text({"keys"}), std::string("(arr) len=1\n(str) a\n(arr) end\n"));
    CHECK_EQ(f.run({"dbsize"}).i, int64_t(2));  // raw count, before the timer collects b
}

TEST(store_zset_commands) {
    Fixture f;
    CHECK_EQ(f.run({"zadd", "z", "1", "a"}).i, int64_t(1));
    CHECK_EQ(f.run({"zadd", "z", "2", "b"}).i, int64_t(1));
    CHECK_EQ(f.run({"zadd", "z", "1.5", "b"}).i, int64_t(0));  // update
    CHECK_EQ(f.run({"zadd", "z", "3", "c"}).i, int64_t(1));
    CHECK_EQ(f.run({"zscore", "z", "b"}).d, 1.5);
    CHECK_EQ(f.text({"zscore", "z", "x"}), std::string("(nil)\n"));
    CHECK_EQ(f.text({"zscore", "nokey", "x"}), std::string("(nil)\n"));
    CHECK_EQ(f.run({"zrank", "z", "c"}).i, int64_t(2));
    CHECK_EQ(f.run({"zcard", "z"}).i, int64_t(3));
    CHECK_EQ(f.run({"zcard", "nokey"}).i, int64_t(0));
    CHECK_EQ(f.text({"zquery", "z", "1.2", "", "0", "10"}),
             std::string("(arr) len=4\n(str) b\n(dbl) 1.5\n(str) c\n(dbl) 3\n(arr) end\n"));
    CHECK_EQ(f.text({"zquery", "z", "1.2", "", "-1", "1"}),
             std::string("(arr) len=2\n(str) a\n(dbl) 1\n(arr) end\n"));
    CHECK_EQ(f.text({"zquery", "z", "0", "", "5", "10"}), std::string("(arr) len=0\n(arr) end\n"));
    CHECK_EQ(f.text({"zquery", "z", "0", "", "0", "0"}), std::string("(arr) len=0\n(arr) end\n"));
    CHECK_EQ(f.text({"zquery", "nokey", "0", "", "0", "9"}), std::string("(arr) len=0\n(arr) end\n"));
    CHECK_EQ(f.run({"zrem", "z", "a"}).i, int64_t(1));
    CHECK_EQ(f.run({"zrem", "z", "a"}).i, int64_t(0));
    f.run({"zrem", "z", "b"});
    f.run({"zrem", "z", "c"});
    CHECK_EQ(f.run({"dbsize"}).i, int64_t(0));  // the emptied set is deleted
}

// Paging: fetch a page, then seek past its last (score, name) to get the next.
TEST(store_zquery_pagination_visits_every_member_once) {
    Fixture f;
    for (int i = 0; i < 95; i++) f.run({"zadd", "z", std::to_string(i % 7), "m" + std::to_string(i)});
    std::string score = "-inf", name;
    int64_t offset = 0;
    size_t seen = 0;
    while (true) {
        Value page = f.run({"zquery", "z", score, name, std::to_string(offset), "10"});
        if (page.arr.empty()) break;
        seen += page.arr.size() / 2;
        name = page.arr[page.arr.size() - 2].s;
        score = std::to_string(page.arr.back().d);
        offset = 1;  // skip the member we resumed from
    }
    CHECK_EQ(seen, size_t(95));
}

TEST(store_large_zset_is_freed_on_the_pool) {
    ThreadPool pool(2);
    Fixture f(&pool);
    for (size_t i = 0; i < k_large_container_size + 500; i++) {
        f.run({"zadd", "big", std::to_string(i), "m" + std::to_string(i)});
    }
    f.run({"zadd", "small", "1", "m"});
    CHECK_EQ(f.run({"del", "big"}).i, int64_t(1));
    CHECK_EQ(f.run({"del", "small"}).i, int64_t(1));
    f.run({"zadd", "big2", "1", "m"});
    for (int i = 0; i < 1500; i++) f.run({"zadd", "big2", "1", "x" + std::to_string(i)});
    f.run({"set", "big2", "str"});  // overwrite also goes through the pool
    CHECK_EQ(f.run({"dbsize"}).i, int64_t(1));
    // The sanitizer builds verify the worker actually frees the memory.
}
