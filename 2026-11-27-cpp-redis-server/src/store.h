// The keyspace and its commands, with no networking in sight.
//
// The event loop hands each parsed request to Store::execute() together with
// the current time, and gets the response bytes back in a buffer. Keeping the
// clock a parameter makes TTL behaviour testable without sleeping.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "hashtable.h"
#include "heap.h"
#include "protocol.h"
#include "thread_pool.h"
#include "zset.h"

enum class EntryType { Str, ZSet };

struct Entry : HNode {
    std::string key;
    EntryType type = EntryType::Str;
    std::string str;
    ZSet zset;
    size_t heap_idx = k_heap_none;  // position of this key's TTL in the heap
};

// Sorted sets bigger than this are freed on a worker thread.
constexpr size_t k_large_container_size = 1000;
// Upper bound on keys expired per call to expire(), so one event-loop pass
// never stalls on a burst of simultaneous deadlines.
constexpr size_t k_max_expire_work = 2000;

class Store {
public:
    explicit Store(ThreadPool *pool) : pool_(pool) {}
    ~Store();
    Store(const Store &) = delete;
    Store &operator=(const Store &) = delete;

    // Runs one command and appends exactly one value to `out`.
    void execute(const std::vector<std::string> &cmd, Buffer &out, uint64_t now_ms);
    // Earliest TTL deadline, or UINT64_MAX if no key has one.
    uint64_t next_deadline() const;
    // Deletes keys whose deadline is <= now. Returns how many.
    size_t expire(uint64_t now_ms, size_t max_work = k_max_expire_work);
    size_t size() const { return hm_size(&db_); }
    size_t ttl_count() const { return heap_.size(); }

private:
    Entry *lookup(const std::string &key, uint64_t now_ms);
    void remove(Entry *ent);
    void set_ttl(Entry *ent, int64_t ttl_ms, uint64_t now_ms);
    void dispose(Entry *ent);

    void do_get(const std::vector<std::string> &cmd, Buffer &out, uint64_t now);
    void do_set(const std::vector<std::string> &cmd, Buffer &out, uint64_t now);
    void do_del(const std::vector<std::string> &cmd, Buffer &out, uint64_t now);
    void do_keys(const std::vector<std::string> &cmd, Buffer &out, uint64_t now);
    void do_dbsize(const std::vector<std::string> &cmd, Buffer &out, uint64_t now);
    bool is_expired(const Entry *ent, uint64_t now) const;
    void do_pexpire(const std::vector<std::string> &cmd, Buffer &out, uint64_t now);
    void do_pttl(const std::vector<std::string> &cmd, Buffer &out, uint64_t now);
    void do_zadd(const std::vector<std::string> &cmd, Buffer &out, uint64_t now);
    void do_zrem(const std::vector<std::string> &cmd, Buffer &out, uint64_t now);
    void do_zscore(const std::vector<std::string> &cmd, Buffer &out, uint64_t now);
    void do_zrank(const std::vector<std::string> &cmd, Buffer &out, uint64_t now);
    void do_zquery(const std::vector<std::string> &cmd, Buffer &out, uint64_t now);
    ZSet *expect_zset(const std::string &key, Buffer &out, uint64_t now, bool &ok);

    HMap db_;
    std::vector<HeapItem> heap_;
    ThreadPool *pool_;
};

bool str2dbl(const std::string &s, double &out);  // rejects NaN and junk
bool str2int(const std::string &s, int64_t &out);
