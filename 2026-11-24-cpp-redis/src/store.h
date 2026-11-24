// The keyspace and every command, with no networking.
//
// Keeping this free of sockets means the command logic can be tested by
// calling execute() directly, and the event loop only moves bytes.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "hashtable.h"
#include "heap.h"
#include "zset.h"

class ThreadPool;

struct Entry : HNode {
    enum class Type { kString, kZSet };
    std::string key;
    Type type = Type::kString;
    std::string str;
    std::unique_ptr<ZSet> zset;
    size_t heap_idx = kNoHeapIdx;  // position of this key's TTL in the heap
};

// Sorted sets bigger than this are freed on a worker thread.
constexpr size_t kLargeContainerSize = 10000;

class Store {
public:
    explicit Store(ThreadPool *pool = nullptr) : pool_(pool) {}
    ~Store();
    Store(const Store &) = delete;
    Store &operator=(const Store &) = delete;

    // Runs one command and appends its RESP reply to `out`. Sets *close_after
    // when the client asked to disconnect (QUIT).
    void execute(const std::vector<std::string> &args, std::string &out, uint64_t now_ms,
                 bool *close_after);

    // Active expiry: delete at most `max_work` keys whose TTL has passed.
    size_t expire_keys(uint64_t now_ms, size_t max_work = 2000);
    // When the next TTL fires (ms), or UINT64_MAX if no key has one.
    uint64_t next_expiry() const { return heap_.empty() ? UINT64_MAX : heap_[0].val; }
    size_t size() const { return hm_size(&db_); }
    size_t ttl_count() const { return heap_.size(); }

    // Commands, public so the dispatch table can name them.
    struct Ctx;
    void cmd_ping(Ctx &c);
    void cmd_echo(Ctx &c);
    void cmd_quit(Ctx &c);
    void cmd_command(Ctx &c);
    void cmd_config(Ctx &c);
    void cmd_get(Ctx &c);
    void cmd_set(Ctx &c);
    void cmd_del(Ctx &c);
    void cmd_exists(Ctx &c);
    void cmd_type(Ctx &c);
    void cmd_keys(Ctx &c);
    void cmd_dbsize(Ctx &c);
    void cmd_flushall(Ctx &c);
    void cmd_expire(Ctx &c);
    void cmd_pexpire(Ctx &c);
    void cmd_ttl(Ctx &c);
    void cmd_pttl(Ctx &c);
    void cmd_persist(Ctx &c);
    void cmd_zadd(Ctx &c);
    void cmd_zincrby(Ctx &c);
    void cmd_zrem(Ctx &c);
    void cmd_zscore(Ctx &c);
    void cmd_zcard(Ctx &c);
    void cmd_zrank(Ctx &c);
    void cmd_zrange(Ctx &c);
    void cmd_zrangebyscore(Ctx &c);
    void cmd_zquery(Ctx &c);

private:
    Entry *lookup(std::string_view key, uint64_t now_ms);  // expired keys count as missing
    Entry *create(std::string_view key);
    ZSet *zset_for_write(Ctx &c, std::string_view key);  // creates if missing
    bool zset_for_read(Ctx &c, std::string_view key, ZSet **out);
    void remove_entry(Entry *entry);
    void set_ttl(Entry *entry, uint64_t now_ms, int64_t ttl_ms);  // ttl_ms < 0 clears it
    void expire_generic(Ctx &c, int64_t multiplier);
    void ttl_generic(Ctx &c, bool millis);

    HMap db_;
    std::vector<HeapItem> heap_;
    ThreadPool *pool_;
};

// Redis-style glob: * ? [abc] [^a] [a-z] and backslash escapes.
bool glob_match(std::string_view pattern, std::string_view str);
