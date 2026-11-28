#include "store.h"

#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>

#include "protocol.h"

namespace {

struct LookupKey : HNode {
    std::string_view key;
};

bool entry_eq(const HNode *stored, const HNode *key) {
    return static_cast<const Entry *>(stored)->key == static_cast<const LookupKey *>(key)->key;
}

bool same_node(const HNode *stored, const HNode *key) { return stored == key; }

bool parse_int(const std::string &s, int64_t &out) {
    if (s.empty() || std::isspace(static_cast<unsigned char>(s[0]))) return false;
    errno = 0;
    char *end = nullptr;
    long long v = std::strtoll(s.c_str(), &end, 10);
    if (errno != 0 || end != s.c_str() + s.size()) return false;  // also rejects embedded NULs
    out = v;
    return true;
}

bool parse_dbl(const std::string &s, double &out) {
    if (s.empty() || std::isspace(static_cast<unsigned char>(s[0]))) return false;
    char *end = nullptr;
    double v = std::strtod(s.c_str(), &end);
    if (end != s.c_str() + s.size() || std::isnan(v)) return false;
    out = v;
    return true;
}

std::string lower(std::string s) {
    for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

constexpr int64_t k_max_ttl_ms = int64_t(1) << 52;  // ~142,000 years; keeps now+ttl from overflowing

}  // namespace

Entry::Entry(std::string_view k) : key(k) { hcode = hash_bytes(k.data(), k.size()); }

Store::~Store() {
    db_.for_each([](HNode *n) {
        delete static_cast<Entry *>(n);
        return true;
    });
    db_.clear();
}

bool Store::expired(const Entry *e, uint64_t now) const {
    return e->heap_idx != k_not_in_heap && heap_[e->heap_idx].val <= now;
}

// Expiry is enforced twice: the timer in expire() removes keys in the
// background, and lookup() refuses to return a key whose time has passed even
// if the timer has not reached it yet (it caps its work per loop iteration).
Entry *Store::lookup(std::string_view key, uint64_t now) {
    LookupKey k;
    k.hcode = hash_bytes(key.data(), key.size());
    k.key = key;
    HNode *found = db_.lookup(&k, entry_eq);
    if (!found) return nullptr;
    auto *e = static_cast<Entry *>(found);
    if (expired(e, now)) {
        remove_entry(e);
        return nullptr;
    }
    return e;
}

void Store::dispose_zset(std::unique_ptr<ZSet> z) {
    if (z && pool_ && z->size() > k_large_container_size) {
        ZSet *raw = z.release();
        pool_->submit([raw] { delete raw; });
    }
    // otherwise `z` frees it right here
}

void Store::remove_entry(Entry *e) {
    db_.remove(e, same_node);
    heap_remove(heap_, e);
    dispose_zset(std::move(e->zset));
    delete e;
}

size_t Store::expire(uint64_t now, size_t max_work) {
    size_t n = 0;
    while (!heap_.empty() && heap_[0].val <= now && n < max_work) {
        remove_entry(static_cast<Entry *>(heap_[0].node));
        n++;
    }
    return n;
}

void Store::execute(const Args &cmd, std::string &out, uint64_t now) {
    if (cmd.empty()) {
        out_err(out, ERR_UNKNOWN, "empty command");
        return;
    }
    struct Spec {
        const char *name;
        size_t min_args, max_args;  // including the command name
    };
    static const Spec specs[] = {
        {"ping", 1, 2},    {"get", 2, 2},    {"set", 3, 3},     {"del", 2, 2},
        {"keys", 1, 1},    {"dbsize", 1, 1}, {"pexpire", 3, 3}, {"persist", 2, 2},
        {"pttl", 2, 2},    {"zadd", 4, 4},   {"zrem", 3, 3},    {"zscore", 3, 3},
        {"zrank", 3, 3},   {"zcard", 2, 2},  {"zquery", 6, 6},
    };
    const std::string name = lower(cmd[0]);
    const Spec *spec = nullptr;
    for (const Spec &s : specs) {
        if (name == s.name) spec = &s;
    }
    if (!spec) {
        out_err(out, ERR_UNKNOWN, "unknown command '" + cmd[0].substr(0, 64) + "'");
        return;
    }
    if (cmd.size() < spec->min_args || cmd.size() > spec->max_args) {
        out_err(out, ERR_BAD_ARG, "wrong number of arguments for '" + name + "'");
        return;
    }

    if (name == "ping") return do_ping(cmd, out);
    if (name == "get") return do_get(cmd, out, now);
    if (name == "set") return do_set(cmd, out, now);
    if (name == "del") return do_del(cmd, out, now);
    if (name == "keys") return do_keys(out, now);
    if (name == "dbsize") return out_int(out, static_cast<int64_t>(db_.size()));
    if (name == "pexpire") return do_pexpire(cmd, out, now);
    if (name == "persist") return do_persist(cmd, out, now);
    if (name == "pttl") return do_pttl(cmd, out, now);
    if (name == "zadd") return do_zadd(cmd, out, now);
    if (name == "zrem") return do_zrem(cmd, out, now);
    if (name == "zscore") return do_zscore(cmd, out, now);
    if (name == "zrank") return do_zrank(cmd, out, now);
    if (name == "zcard") return do_zcard(cmd, out, now);
    if (name == "zquery") return do_zquery(cmd, out, now);
}

void Store::do_ping(const Args &a, std::string &out) { out_str(out, a.size() == 2 ? a[1] : "PONG"); }

void Store::do_get(const Args &a, std::string &out, uint64_t now) {
    Entry *e = lookup(a[1], now);
    if (!e) return out_nil(out);
    if (e->type != Type::Str) return out_err(out, ERR_BAD_TYP, "expect string type");
    out_str(out, e->str);
}

// Like Redis: SET replaces a value of any type and clears any TTL.
void Store::do_set(const Args &a, std::string &out, uint64_t now) {
    Entry *e = lookup(a[1], now);
    if (!e) {
        e = new Entry(a[1]);
        db_.insert(e);
    } else {
        heap_remove(heap_, e);
        if (e->type == Type::ZSet) {
            dispose_zset(std::move(e->zset));
            e->type = Type::Str;
        }
    }
    e->str = a[2];
    out_nil(out);
}

void Store::do_del(const Args &a, std::string &out, uint64_t now) {
    Entry *e = lookup(a[1], now);
    if (!e) return out_int(out, 0);
    remove_entry(e);
    out_int(out, 1);
}

void Store::do_keys(std::string &out, uint64_t now) {
    std::vector<const std::string *> live;
    live.reserve(db_.size());
    db_.for_each([&](HNode *n) {
        const auto *e = static_cast<const Entry *>(n);
        if (!expired(e, now)) live.push_back(&e->key);
        return true;
    });
    out_arr(out, static_cast<uint32_t>(live.size()));
    for (const std::string *k : live) out_str(out, *k);
}

// Like Redis: a TTL of zero or less deletes the key now.
void Store::do_pexpire(const Args &a, std::string &out, uint64_t now) {
    int64_t ttl = 0;
    if (!parse_int(a[2], ttl)) return out_err(out, ERR_BAD_ARG, "expect int64");
    Entry *e = lookup(a[1], now);
    if (!e) return out_int(out, 0);
    if (ttl <= 0) {
        remove_entry(e);
    } else {
        if (ttl > k_max_ttl_ms) ttl = k_max_ttl_ms;
        heap_set(heap_, e, now + static_cast<uint64_t>(ttl));
    }
    out_int(out, 1);
}

void Store::do_persist(const Args &a, std::string &out, uint64_t now) {
    Entry *e = lookup(a[1], now);
    if (!e || e->heap_idx == k_not_in_heap) return out_int(out, 0);
    heap_remove(heap_, e);
    out_int(out, 1);
}

void Store::do_pttl(const Args &a, std::string &out, uint64_t now) {
    Entry *e = lookup(a[1], now);
    if (!e) return out_int(out, -2);
    if (e->heap_idx == k_not_in_heap) return out_int(out, -1);
    out_int(out, static_cast<int64_t>(heap_[e->heap_idx].val - now));
}

bool Store::find_zset(const std::string &key, uint64_t now, std::string &out, Entry *&ent) {
    ent = lookup(key, now);
    if (ent && ent->type != Type::ZSet) {
        out_err(out, ERR_BAD_TYP, "expect zset");
        return false;
    }
    return true;
}

void Store::do_zadd(const Args &a, std::string &out, uint64_t now) {
    double score = 0;
    if (!parse_dbl(a[2], score)) return out_err(out, ERR_BAD_ARG, "expect float");
    Entry *e = lookup(a[1], now);
    if (!e) {
        e = new Entry(a[1]);
        e->type = Type::ZSet;
        e->zset = std::make_unique<ZSet>();
        db_.insert(e);
    } else if (e->type != Type::ZSet) {
        return out_err(out, ERR_BAD_TYP, "expect zset");
    }
    out_int(out, e->zset->insert(a[3], score) ? 1 : 0);
}

void Store::do_zrem(const Args &a, std::string &out, uint64_t now) {
    Entry *e = nullptr;
    if (!find_zset(a[1], now, out, e)) return;
    bool removed = e && e->zset->remove(a[2]);
    if (e && e->zset->size() == 0) remove_entry(e);  // like Redis, no empty sets
    out_int(out, removed ? 1 : 0);
}

void Store::do_zscore(const Args &a, std::string &out, uint64_t now) {
    Entry *e = nullptr;
    if (!find_zset(a[1], now, out, e)) return;
    ZNode *n = e ? e->zset->lookup(a[2]) : nullptr;
    if (!n) return out_nil(out);
    out_dbl(out, n->score);
}

void Store::do_zrank(const Args &a, std::string &out, uint64_t now) {
    Entry *e = nullptr;
    if (!find_zset(a[1], now, out, e)) return;
    ZNode *n = e ? e->zset->lookup(a[2]) : nullptr;
    if (!n) return out_nil(out);
    out_int(out, ZSet::rank(n));
}

void Store::do_zcard(const Args &a, std::string &out, uint64_t now) {
    Entry *e = nullptr;
    if (!find_zset(a[1], now, out, e)) return;
    out_int(out, e ? static_cast<int64_t>(e->zset->size()) : 0);
}

// zquery key score name offset limit
// Seek to the first member >= (score, name), skip `offset` members (negative
// goes backwards), then return up to `limit` members as name, score pairs.
// This is how you page through a sorted set without re-scanning from the start.
void Store::do_zquery(const Args &a, std::string &out, uint64_t now) {
    double score = 0;
    int64_t offset = 0, limit = 0;
    if (!parse_dbl(a[2], score)) return out_err(out, ERR_BAD_ARG, "expect float");
    if (!parse_int(a[4], offset) || !parse_int(a[5], limit)) {
        return out_err(out, ERR_BAD_ARG, "expect int64");
    }
    Entry *e = nullptr;
    if (!find_zset(a[1], now, out, e)) return;
    if (!e || limit <= 0) return out_arr(out, 0);
    ZNode *n = ZSet::offset(e->zset->seek_ge(score, a[3]), offset);
    size_t ctx = out_begin_arr(out);
    uint32_t count = 0;
    for (int64_t i = 0; n && i < limit; i++) {
        out_str(out, n->name);
        out_dbl(out, n->score);
        count += 2;
        n = ZSet::offset(n, 1);
    }
    out_end_arr(out, ctx, count);
}
