#include "store.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <strings.h>

namespace {
struct KeyProbe : HNode {
    const std::string *key;
};
}  // namespace

static bool entry_eq(HNode *stored, HNode *probe) {
    return static_cast<Entry *>(stored)->key == *static_cast<KeyProbe *>(probe)->key;
}

bool str2dbl(const std::string &s, double &out) {
    if (s.empty()) return false;
    char *end = nullptr;
    out = strtod(s.c_str(), &end);
    return end == s.c_str() + s.size() && !std::isnan(out);
}

bool str2int(const std::string &s, int64_t &out) {
    if (s.empty()) return false;
    char *end = nullptr;
    errno = 0;
    long long v = strtoll(s.c_str(), &end, 10);
    if (errno == ERANGE || end != s.c_str() + s.size()) return false;
    out = v;
    return true;
}

Store::~Store() {
    std::vector<Entry *> all;
    hm_foreach(&db_, [&](HNode *n) {
        all.push_back(static_cast<Entry *>(n));
        return true;
    });
    hm_clear(&db_);
    heap_.clear();
    for (Entry *e : all) {
        if (e->type == EntryType::ZSet) zset_clear(&e->zset);
        delete e;
    }
}

// Finds a live key. A key whose deadline has passed is deleted on the spot,
// even if the timer has not got round to it yet ("lazy" expiry).
Entry *Store::lookup(const std::string &key, uint64_t now_ms) {
    KeyProbe probe;
    probe.hcode = str_hash(key.data(), key.size());
    probe.key = &key;
    HNode *node = hm_lookup(&db_, &probe, &entry_eq);
    if (!node) return nullptr;
    Entry *ent = static_cast<Entry *>(node);
    if (is_expired(ent, now_ms)) {
        remove(ent);
        return nullptr;
    }
    return ent;
}

void Store::set_ttl(Entry *ent, int64_t ttl_ms, uint64_t now_ms) {
    if (ttl_ms < 0) {
        if (ent->heap_idx != k_heap_none) heap_delete(heap_, ent->heap_idx);
        return;
    }
    HeapItem item;
    item.val = now_ms + static_cast<uint64_t>(ttl_ms);
    item.ref = &ent->heap_idx;
    item.data = ent;
    heap_upsert(heap_, ent->heap_idx, item);
}

void Store::dispose(Entry *ent) {
    if (ent->type == EntryType::ZSet && zset_size(&ent->zset) > k_large_container_size && pool_) {
        // Freeing a million nodes takes a while; don't make other clients wait.
        pool_->submit([ent] {
            zset_clear(&ent->zset);
            delete ent;
        });
        return;
    }
    if (ent->type == EntryType::ZSet) zset_clear(&ent->zset);
    delete ent;
}

// Unlinks from the keyspace and the TTL heap, then frees.
void Store::remove(Entry *ent) {
    KeyProbe probe;
    probe.hcode = ent->hcode;
    probe.key = &ent->key;
    hm_delete(&db_, &probe, &entry_eq);
    if (ent->heap_idx != k_heap_none) heap_delete(heap_, ent->heap_idx);
    dispose(ent);
}

uint64_t Store::next_deadline() const { return heap_.empty() ? UINT64_MAX : heap_[0].val; }

size_t Store::expire(uint64_t now_ms, size_t max_work) {
    size_t n = 0;
    while (n < max_work && !heap_.empty() && heap_[0].val <= now_ms) {
        remove(static_cast<Entry *>(heap_[0].data));
        n++;
    }
    return n;
}

void Store::do_get(const std::vector<std::string> &cmd, Buffer &out, uint64_t now) {
    Entry *ent = lookup(cmd[1], now);
    if (!ent) return out_nil(out);
    if (ent->type != EntryType::Str) return out_err(out, ERR_BAD_TYP, "not a string value");
    out_str(out, ent->str);
}

// Like Redis: SET replaces a value of any type and clears its TTL.
void Store::do_set(const std::vector<std::string> &cmd, Buffer &out, uint64_t now) {
    Entry *ent = lookup(cmd[1], now);
    if (ent) {
        if (ent->type == EntryType::ZSet) {
            zset_clear(&ent->zset);
            ent->type = EntryType::Str;
        }
        set_ttl(ent, -1, now);
        ent->str = cmd[2];
    } else {
        ent = new Entry;
        ent->key = cmd[1];
        ent->hcode = str_hash(ent->key.data(), ent->key.size());
        ent->str = cmd[2];
        hm_insert(&db_, ent);
    }
    out_nil(out);
}

void Store::do_del(const std::vector<std::string> &cmd, Buffer &out, uint64_t now) {
    Entry *ent = lookup(cmd[1], now);
    if (ent) remove(ent);
    out_int(out, ent ? 1 : 0);
}

bool Store::is_expired(const Entry *ent, uint64_t now) const {
    return ent->heap_idx != k_heap_none && heap_[ent->heap_idx].val <= now;
}

// Lists live keys. Expired keys the timer has not reached yet are skipped,
// not deleted: deleting would modify the table while we walk it.
void Store::do_keys(const std::vector<std::string> &, Buffer &out, uint64_t now) {
    size_t ctx = out_begin_arr(out);
    uint32_t n = 0;
    hm_foreach(&db_, [&](HNode *node) {
        Entry *ent = static_cast<Entry *>(node);
        if (!is_expired(ent, now)) {
            out_str(out, ent->key);
            n++;
        }
        return true;
    });
    out_end_arr(out, ctx, n);
}

void Store::do_dbsize(const std::vector<std::string> &, Buffer &out, uint64_t now) {
    int64_t n = 0;
    hm_foreach(&db_, [&](HNode *node) {
        n += !is_expired(static_cast<Entry *>(node), now);
        return true;
    });
    out_int(out, n);
}

// PEXPIRE key ms: ms >= 0 sets a deadline, a negative ms removes it.
void Store::do_pexpire(const std::vector<std::string> &cmd, Buffer &out, uint64_t now) {
    int64_t ttl = 0;
    if (!str2int(cmd[2], ttl)) return out_err(out, ERR_BAD_ARG, "expect int64");
    if (ttl > (int64_t(1) << 52)) return out_err(out, ERR_BAD_ARG, "ttl is too large");
    Entry *ent = lookup(cmd[1], now);
    if (ent) set_ttl(ent, ttl, now);
    out_int(out, ent ? 1 : 0);
}

// -2: no such key, -1: no TTL, otherwise milliseconds left.
void Store::do_pttl(const std::vector<std::string> &cmd, Buffer &out, uint64_t now) {
    Entry *ent = lookup(cmd[1], now);
    if (!ent) return out_int(out, -2);
    if (ent->heap_idx == k_heap_none) return out_int(out, -1);
    out_int(out, static_cast<int64_t>(heap_[ent->heap_idx].val - now));
}

// Returns the sorted set at `key`, or null for a missing key. Writes an error
// and clears `ok` if the key holds a string.
ZSet *Store::expect_zset(const std::string &key, Buffer &out, uint64_t now, bool &ok) {
    ok = true;
    Entry *ent = lookup(key, now);
    if (!ent) return nullptr;
    if (ent->type != EntryType::ZSet) {
        ok = false;
        out_err(out, ERR_BAD_TYP, "expect zset");
        return nullptr;
    }
    return &ent->zset;
}

void Store::do_zadd(const std::vector<std::string> &cmd, Buffer &out, uint64_t now) {
    double score = 0;
    if (!str2dbl(cmd[2], score)) return out_err(out, ERR_BAD_ARG, "expect float");
    bool ok;
    ZSet *zset = expect_zset(cmd[1], out, now, ok);
    if (!ok) return;
    if (!zset) {
        Entry *ent = new Entry;
        ent->key = cmd[1];
        ent->hcode = str_hash(ent->key.data(), ent->key.size());
        ent->type = EntryType::ZSet;
        hm_insert(&db_, ent);
        zset = &ent->zset;
    }
    out_int(out, zset_insert(zset, cmd[3], score) ? 1 : 0);
}

void Store::do_zrem(const std::vector<std::string> &cmd, Buffer &out, uint64_t now) {
    bool ok;
    ZSet *zset = expect_zset(cmd[1], out, now, ok);
    if (!ok) return;
    out_int(out, zset && zset_delete(zset, cmd[2]) ? 1 : 0);
}

void Store::do_zscore(const std::vector<std::string> &cmd, Buffer &out, uint64_t now) {
    bool ok;
    ZSet *zset = expect_zset(cmd[1], out, now, ok);
    if (!ok) return;
    ZNode *node = zset ? zset_lookup(zset, cmd[2]) : nullptr;
    if (!node) return out_nil(out);
    out_dbl(out, node->score);
}

void Store::do_zrank(const std::vector<std::string> &cmd, Buffer &out, uint64_t now) {
    bool ok;
    ZSet *zset = expect_zset(cmd[1], out, now, ok);
    if (!ok) return;
    ZNode *node = zset ? zset_lookup(zset, cmd[2]) : nullptr;
    if (!node) return out_nil(out);
    out_int(out, znode_rank(node));
}

// ZQUERY key score name offset limit
// Seeks to the first member >= (score, name), moves `offset` places (which may
// be negative), then returns up to `limit` pairs as a flat [name, score, ...].
void Store::do_zquery(const std::vector<std::string> &cmd, Buffer &out, uint64_t now) {
    double score = 0;
    if (!str2dbl(cmd[2], score)) return out_err(out, ERR_BAD_ARG, "expect float");
    int64_t offset = 0, limit = 0;
    if (!str2int(cmd[4], offset) || !str2int(cmd[5], limit)) {
        return out_err(out, ERR_BAD_ARG, "expect int");
    }
    bool ok;
    ZSet *zset = expect_zset(cmd[1], out, now, ok);
    if (!ok) return;
    if (!zset || limit <= 0) return out_arr(out, 0);

    ZNode *node = znode_offset(zset_seekge(zset, score, cmd[3]), offset);
    size_t ctx = out_begin_arr(out);
    int64_t pairs = 0;
    while (node && pairs < limit) {
        out_str(out, znode_name(node));
        out_dbl(out, node->score);
        node = znode_offset(node, 1);
        pairs++;
    }
    out_end_arr(out, ctx, static_cast<uint32_t>(pairs * 2));
}

namespace {
using Handler = void (Store::*)(const std::vector<std::string> &, Buffer &, uint64_t);
struct CommandSpec {
    const char *name;
    size_t argc;  // including the command name
    Handler fn;
};
}  // namespace

void Store::execute(const std::vector<std::string> &cmd, Buffer &out, uint64_t now_ms) {
    static const CommandSpec k_commands[] = {
        {"get", 2, &Store::do_get},         {"set", 3, &Store::do_set},
        {"del", 2, &Store::do_del},         {"pexpire", 3, &Store::do_pexpire},
        {"pttl", 2, &Store::do_pttl},       {"zadd", 4, &Store::do_zadd},
        {"zrem", 3, &Store::do_zrem},       {"zscore", 3, &Store::do_zscore},
        {"zrank", 3, &Store::do_zrank},     {"zquery", 6, &Store::do_zquery},
        {"keys", 1, &Store::do_keys},       {"dbsize", 1, &Store::do_dbsize},
    };
    if (cmd.empty()) return out_err(out, ERR_UNKNOWN, "empty command");
    if (strcasecmp(cmd[0].c_str(), "ping") == 0) {  // PING [message]
        if (cmd.size() > 2) return out_err(out, ERR_BAD_ARG, "wrong number of arguments");
        return out_str(out, cmd.size() == 2 ? cmd[1] : "PONG");
    }
    for (const auto &c : k_commands) {
        if (strcasecmp(cmd[0].c_str(), c.name) != 0) continue;
        if (cmd.size() != c.argc) return out_err(out, ERR_BAD_ARG, "wrong number of arguments");
        return (this->*c.fn)(cmd, out, now_ms);
    }
    out_err(out, ERR_UNKNOWN, "unknown command");
}
