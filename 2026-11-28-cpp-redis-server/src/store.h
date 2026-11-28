// The keyspace and every command, with no networking. The server feeds it
// parsed requests and the current time; tests feed it a fake clock.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "hashtable.h"
#include "heap.h"
#include "thread_pool.h"
#include "zset.h"

enum class Type { Str, ZSet };

struct Entry : HNode, HeapNode {
    std::string key;
    Type type = Type::Str;
    std::string str;
    std::unique_ptr<ZSet> zset;
    explicit Entry(std::string_view k);
};

// Sorted sets bigger than this are freed on a worker thread.
constexpr size_t k_large_container_size = 1000;

class Store {
public:
    explicit Store(ThreadPool *pool = nullptr) : pool_(pool) {}
    ~Store();
    Store(const Store &) = delete;
    Store &operator=(const Store &) = delete;

    // Runs one command and appends exactly one serialized value to `out`.
    void execute(const std::vector<std::string> &cmd, std::string &out, uint64_t now_ms);

    // Earliest pending expiry (absolute ms), or -1 if no key has a TTL.
    int64_t next_expiry() const { return heap_.empty() ? -1 : static_cast<int64_t>(heap_[0].val); }
    // Deletes up to `max_work` keys whose TTL has passed. Returns how many.
    size_t expire(uint64_t now_ms, size_t max_work);
    size_t size() const { return db_.size(); }
    size_t ttl_count() const { return heap_.size(); }

private:
    ThreadPool *pool_;
    HMap db_;
    std::vector<HeapItem> heap_;

    using Args = std::vector<std::string>;
    Entry *lookup(std::string_view key, uint64_t now);
    bool expired(const Entry *e, uint64_t now) const;
    void remove_entry(Entry *e);
    void dispose_zset(std::unique_ptr<ZSet> z);
    // Finds a sorted set for a read or delete. Missing key -> true with *ent null.
    // Wrong type -> writes the error and returns false.
    bool find_zset(const std::string &key, uint64_t now, std::string &out, Entry *&ent);

    void do_ping(const Args &a, std::string &out);
    void do_get(const Args &a, std::string &out, uint64_t now);
    void do_set(const Args &a, std::string &out, uint64_t now);
    void do_del(const Args &a, std::string &out, uint64_t now);
    void do_keys(std::string &out, uint64_t now);
    void do_pexpire(const Args &a, std::string &out, uint64_t now);
    void do_persist(const Args &a, std::string &out, uint64_t now);
    void do_pttl(const Args &a, std::string &out, uint64_t now);
    void do_zadd(const Args &a, std::string &out, uint64_t now);
    void do_zrem(const Args &a, std::string &out, uint64_t now);
    void do_zscore(const Args &a, std::string &out, uint64_t now);
    void do_zrank(const Args &a, std::string &out, uint64_t now);
    void do_zcard(const Args &a, std::string &out, uint64_t now);
    void do_zquery(const Args &a, std::string &out, uint64_t now);
};
