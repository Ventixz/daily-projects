#include "store.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <strings.h>

#include "resp.h"
#include "thread_pool.h"

struct Store::Ctx {
    const std::vector<std::string> &args;
    Reply reply;
    uint64_t now;
    bool close_after = false;
};

namespace {

struct LookupKey : HNode {
    std::string_view key;
};

bool entry_eq(const HNode *node, const HNode *key) {
    return static_cast<const Entry *>(node)->key == static_cast<const LookupKey *>(key)->key;
}

uint64_t key_hash(std::string_view key) {
    return str_hash(reinterpret_cast<const uint8_t *>(key.data()), key.size());
}

LookupKey make_key(std::string_view key) {
    LookupKey k;
    k.hcode = key_hash(key);
    k.key = key;
    return k;
}

const char *kWrongType = "WRONGTYPE Operation against a key holding the wrong kind of value";
const char *kNotInt = "ERR value is not an integer or out of range";
const char *kNotFloat = "ERR value is not a valid float";
const char *kSyntax = "ERR syntax error";

bool parse_int(std::string_view s, int64_t *out) {
    if (s.empty()) return false;
    const char *first = s.data(), *last = s.data() + s.size();
    if (*first == '+') return false;  // Redis rejects a leading '+'
    auto [p, ec] = std::from_chars(first, last, *out);
    return ec == std::errc() && p == last;
}

bool parse_double(std::string_view s, double *out) {
    if (s.empty() || s.size() > 256) return false;
    std::string tmp(s);  // strtod needs a NUL terminator
    char *end = nullptr;
    *out = strtod(tmp.c_str(), &end);
    if (end != tmp.c_str() + tmp.size()) return false;
    if (isspace(static_cast<unsigned char>(tmp[0]))) return false;
    return !std::isnan(*out);
}

bool ieq(std::string_view a, const char *b) {
    size_t n = strlen(b);
    return a.size() == n && strncasecmp(a.data(), b, n) == 0;
}

// A score bound for ZRANGEBYSCORE: "1.5", "(1.5" (exclusive), "-inf", "+inf".
bool parse_bound(std::string_view s, double *v, bool *exclusive) {
    *exclusive = !s.empty() && s[0] == '(';
    if (*exclusive) s.remove_prefix(1);
    return parse_double(s, v);
}

struct Command {
    const char *name;
    int arity;  // > 0: exact argc; < 0: at least -arity
    void (Store::*fn)(Store::Ctx &);
};

const Command kCommands[] = {
    {"ping", -1, &Store::cmd_ping},
    {"echo", 2, &Store::cmd_echo},
    {"quit", -1, &Store::cmd_quit},
    {"command", -1, &Store::cmd_command},
    {"config", -2, &Store::cmd_config},
    {"get", 2, &Store::cmd_get},
    {"set", -3, &Store::cmd_set},
    {"del", -2, &Store::cmd_del},
    {"exists", -2, &Store::cmd_exists},
    {"type", 2, &Store::cmd_type},
    {"keys", 2, &Store::cmd_keys},
    {"dbsize", 1, &Store::cmd_dbsize},
    {"flushall", -1, &Store::cmd_flushall},
    {"flushdb", -1, &Store::cmd_flushall},
    {"expire", 3, &Store::cmd_expire},
    {"pexpire", 3, &Store::cmd_pexpire},
    {"ttl", 2, &Store::cmd_ttl},
    {"pttl", 2, &Store::cmd_pttl},
    {"persist", 2, &Store::cmd_persist},
    {"zadd", -4, &Store::cmd_zadd},
    {"zincrby", 4, &Store::cmd_zincrby},
    {"zrem", -3, &Store::cmd_zrem},
    {"zscore", 3, &Store::cmd_zscore},
    {"zcard", 2, &Store::cmd_zcard},
    {"zrank", 3, &Store::cmd_zrank},
    {"zrange", -4, &Store::cmd_zrange},
    {"zrangebyscore", -4, &Store::cmd_zrangebyscore},
    {"zquery", 6, &Store::cmd_zquery},
};

const Command *find_command(std::string_view name) {
    for (const Command &cmd : kCommands) {
        if (ieq(name, cmd.name)) return &cmd;
    }
    return nullptr;
}

bool free_entry_cb(HNode *node, void *) {
    delete static_cast<Entry *>(node);
    return true;
}

}  // namespace

Store::~Store() {
    hm_foreach(&db_, &free_entry_cb, nullptr);
    hm_clear(&db_);
}

void Store::execute(const std::vector<std::string> &args, std::string &out, uint64_t now_ms,
                    bool *close_after) {
    Ctx c{args, Reply(out), now_ms};
    *close_after = false;
    if (args.empty()) return;
    const Command *cmd = find_command(args[0]);
    if (!cmd) {
        std::string msg = "ERR unknown command '" + args[0] + "', with args beginning with: ";
        for (size_t i = 1; i < args.size() && msg.size() < 128; i++) msg += "'" + args[i] + "' ";
        c.reply.error(msg);
        return;
    }
    int argc = static_cast<int>(args.size());
    if ((cmd->arity > 0 && argc != cmd->arity) || (cmd->arity < 0 && argc < -cmd->arity)) {
        c.reply.error(std::string("ERR wrong number of arguments for '") + cmd->name + "' command");
        return;
    }
    (this->*cmd->fn)(c);
    *close_after = c.close_after;
}

// ---- keyspace plumbing ----------------------------------------------------

Entry *Store::lookup(std::string_view key, uint64_t now_ms) {
    LookupKey k = make_key(key);
    HNode *node = hm_lookup(&db_, &k, &entry_eq);
    if (!node) return nullptr;
    auto *entry = static_cast<Entry *>(node);
    if (entry->heap_idx != kNoHeapIdx && heap_[entry->heap_idx].val <= now_ms) {
        remove_entry(entry);  // lazy expiry: never hand out a dead key
        return nullptr;
    }
    return entry;
}

Entry *Store::create(std::string_view key) {
    auto *entry = new Entry;
    entry->key = std::string(key);
    entry->hcode = key_hash(key);
    hm_insert(&db_, entry);
    return entry;
}

void Store::remove_entry(Entry *entry) {
    LookupKey k = make_key(entry->key);
    hm_delete(&db_, &k, &entry_eq);
    heap_remove(heap_, &entry->heap_idx);
    if (pool_ && entry->zset && entry->zset->size() > kLargeContainerSize) {
        // Unreachable from now on, so a worker can free it without locking.
        ZSet *zset = entry->zset.release();
        pool_->submit([zset] { delete zset; });
    }
    delete entry;
}

void Store::set_ttl(Entry *entry, uint64_t now_ms, int64_t ttl_ms) {
    if (ttl_ms < 0) {
        heap_remove(heap_, &entry->heap_idx);
    } else {
        heap_upsert(heap_, &entry->heap_idx, entry, now_ms + static_cast<uint64_t>(ttl_ms));
    }
}

size_t Store::expire_keys(uint64_t now_ms, size_t max_work) {
    size_t n = 0;
    while (n < max_work && !heap_.empty() && heap_[0].val <= now_ms) {
        remove_entry(static_cast<Entry *>(heap_[0].owner));
        n++;
    }
    return n;
}

// ---- connection / server commands -----------------------------------------

void Store::cmd_ping(Ctx &c) {
    if (c.args.size() > 2) {
        c.reply.error("ERR wrong number of arguments for 'ping' command");
    } else if (c.args.size() == 2) {
        c.reply.bulk(c.args[1]);
    } else {
        c.reply.simple("PONG");
    }
}

void Store::cmd_echo(Ctx &c) { c.reply.bulk(c.args[1]); }

void Store::cmd_quit(Ctx &c) {
    c.reply.simple("OK");
    c.close_after = true;
}

// redis-cli asks for command docs on startup and redis-benchmark reads its
// config; an empty answer is enough to keep both happy.
void Store::cmd_command(Ctx &c) { c.reply.array(0); }
void Store::cmd_config(Ctx &c) { c.reply.array(0); }

// ---- strings ----------------------------------------------------------------

void Store::cmd_get(Ctx &c) {
    Entry *e = lookup(c.args[1], c.now);
    if (!e) return c.reply.nil();
    if (e->type != Entry::Type::kString) return c.reply.error(kWrongType);
    c.reply.bulk(e->str);
}

void Store::cmd_set(Ctx &c) {
    bool nx = false, xx = false, keepttl = false;
    int64_t ttl_ms = -1;
    for (size_t i = 3; i < c.args.size(); i++) {
        const std::string &opt = c.args[i];
        if (ieq(opt, "NX") && !xx) {
            nx = true;
        } else if (ieq(opt, "XX") && !nx) {
            xx = true;
        } else if (ieq(opt, "KEEPTTL") && ttl_ms < 0) {
            keepttl = true;
        } else if ((ieq(opt, "EX") || ieq(opt, "PX")) && !keepttl && ttl_ms < 0 && i + 1 < c.args.size()) {
            int64_t v = 0;
            if (!parse_int(c.args[++i], &v)) return c.reply.error(kNotInt);
            int64_t mult = ieq(opt, "EX") ? 1000 : 1;
            if (v <= 0 || v > INT64_MAX / mult / 2) return c.reply.error("ERR invalid expire time in 'set' command");
            ttl_ms = v * mult;
        } else {
            return c.reply.error(kSyntax);
        }
    }
    Entry *e = lookup(c.args[1], c.now);
    if ((nx && e) || (xx && !e)) return c.reply.nil();
    if (!e) e = create(c.args[1]);
    e->type = Entry::Type::kString;
    e->zset.reset();
    e->str = c.args[2];
    if (ttl_ms >= 0) {
        set_ttl(e, c.now, ttl_ms);
    } else if (!keepttl) {
        set_ttl(e, c.now, -1);
    }
    c.reply.simple("OK");
}

// ---- generic key commands ---------------------------------------------------

void Store::cmd_del(Ctx &c) {
    int64_t n = 0;
    for (size_t i = 1; i < c.args.size(); i++) {
        if (Entry *e = lookup(c.args[i], c.now)) {
            remove_entry(e);
            n++;
        }
    }
    c.reply.integer(n);
}

void Store::cmd_exists(Ctx &c) {
    int64_t n = 0;
    for (size_t i = 1; i < c.args.size(); i++) n += lookup(c.args[i], c.now) ? 1 : 0;
    c.reply.integer(n);
}

void Store::cmd_type(Ctx &c) {
    Entry *e = lookup(c.args[1], c.now);
    if (!e) return c.reply.simple("none");
    c.reply.simple(e->type == Entry::Type::kString ? "string" : "zset");
}

void Store::cmd_keys(Ctx &c) {
    struct Scan {
        const Store *store;
        std::string_view pattern;
        uint64_t now;
        std::vector<const std::string *> keys;
    } scan{this, c.args[1], c.now, {}};
    hm_foreach(
        &db_,
        [](HNode *node, void *arg) {
            auto *s = static_cast<Scan *>(arg);
            auto *e = static_cast<Entry *>(node);
            // Skip expired keys without deleting them: hm_foreach forbids mutation.
            bool expired = e->heap_idx != kNoHeapIdx && s->store->heap_[e->heap_idx].val <= s->now;
            if (!expired && glob_match(s->pattern, e->key)) s->keys.push_back(&e->key);
            return true;
        },
        &scan);
    c.reply.array(scan.keys.size());
    for (const std::string *k : scan.keys) c.reply.bulk(*k);
}

void Store::cmd_dbsize(Ctx &c) {
    // Like Redis, this may still count keys that expired but were not yet reaped.
    c.reply.integer(static_cast<int64_t>(size()));
}

void Store::cmd_flushall(Ctx &c) {
    std::vector<Entry *> all;
    hm_foreach(
        &db_,
        [](HNode *node, void *arg) {
            static_cast<std::vector<Entry *> *>(arg)->push_back(static_cast<Entry *>(node));
            return true;
        },
        &all);
    for (Entry *e : all) remove_entry(e);
    c.reply.simple("OK");
}

void Store::expire_generic(Ctx &c, int64_t multiplier) {
    int64_t v = 0;
    if (!parse_int(c.args[2], &v)) return c.reply.error(kNotInt);
    if (v > INT64_MAX / multiplier / 2 || v < INT64_MIN / multiplier / 2) {
        return c.reply.error(std::string("ERR invalid expire time in '") + (multiplier == 1 ? "pexpire" : "expire") +
                             "' command");
    }
    Entry *e = lookup(c.args[1], c.now);
    if (!e) return c.reply.integer(0);
    int64_t ttl_ms = v * multiplier;
    if (ttl_ms <= 0) {
        remove_entry(e);  // a TTL in the past deletes the key, as in Redis
    } else {
        set_ttl(e, c.now, ttl_ms);
    }
    c.reply.integer(1);
}

void Store::cmd_expire(Ctx &c) { expire_generic(c, 1000); }
void Store::cmd_pexpire(Ctx &c) { expire_generic(c, 1); }

void Store::ttl_generic(Ctx &c, bool millis) {
    Entry *e = lookup(c.args[1], c.now);
    if (!e) return c.reply.integer(-2);
    if (e->heap_idx == kNoHeapIdx) return c.reply.integer(-1);
    int64_t left = static_cast<int64_t>(heap_[e->heap_idx].val - c.now);
    c.reply.integer(millis ? left : (left + 500) / 1000);
}

void Store::cmd_ttl(Ctx &c) { ttl_generic(c, false); }
void Store::cmd_pttl(Ctx &c) { ttl_generic(c, true); }

void Store::cmd_persist(Ctx &c) {
    Entry *e = lookup(c.args[1], c.now);
    if (!e || e->heap_idx == kNoHeapIdx) return c.reply.integer(0);
    set_ttl(e, c.now, -1);
    c.reply.integer(1);
}

// ---- sorted sets ------------------------------------------------------------

ZSet *Store::zset_for_write(Ctx &c, std::string_view key) {
    Entry *e = lookup(key, c.now);
    if (e && e->type != Entry::Type::kZSet) {
        c.reply.error(kWrongType);
        return nullptr;
    }
    if (!e) {
        e = create(key);
        e->type = Entry::Type::kZSet;
        e->zset = std::make_unique<ZSet>();
    }
    return e->zset.get();
}

// Returns false (after replying) on a type error. *out is nullptr if missing.
bool Store::zset_for_read(Ctx &c, std::string_view key, ZSet **out) {
    Entry *e = lookup(key, c.now);
    *out = nullptr;
    if (!e) return true;
    if (e->type != Entry::Type::kZSet) {
        c.reply.error(kWrongType);
        return false;
    }
    *out = e->zset.get();
    return true;
}

void Store::cmd_zadd(Ctx &c) {
    if ((c.args.size() - 2) % 2 != 0) return c.reply.error(kSyntax);
    std::vector<double> scores;
    for (size_t i = 2; i < c.args.size(); i += 2) {  // validate everything before writing
        double s = 0;
        if (!parse_double(c.args[i], &s)) return c.reply.error(kNotFloat);
        scores.push_back(s);
    }
    ZSet *z = zset_for_write(c, c.args[1]);
    if (!z) return;
    int64_t added = 0;
    for (size_t i = 2, j = 0; i < c.args.size(); i += 2, j++) added += z->insert(c.args[i + 1], scores[j]) ? 1 : 0;
    c.reply.integer(added);
}

void Store::cmd_zincrby(Ctx &c) {
    double incr = 0;
    if (!parse_double(c.args[2], &incr)) return c.reply.error(kNotFloat);
    ZSet *z = zset_for_write(c, c.args[1]);
    if (!z) return;
    ZNode *node = z->lookup(c.args[3]);
    double score = (node ? node->score : 0) + incr;
    if (std::isnan(score)) return c.reply.error("ERR resulting score is not a number (NaN)");
    z->insert(c.args[3], score);
    c.reply.dbl(score);
}

void Store::cmd_zrem(Ctx &c) {
    Entry *e = lookup(c.args[1], c.now);
    if (!e) return c.reply.integer(0);
    if (e->type != Entry::Type::kZSet) return c.reply.error(kWrongType);
    int64_t n = 0;
    for (size_t i = 2; i < c.args.size(); i++) {
        if (ZNode *node = e->zset->lookup(c.args[i])) {
            e->zset->remove(node);
            n++;
        }
    }
    if (e->zset->size() == 0) remove_entry(e);  // empty sets don't exist in Redis
    c.reply.integer(n);
}

void Store::cmd_zscore(Ctx &c) {
    ZSet *z;
    if (!zset_for_read(c, c.args[1], &z)) return;
    ZNode *node = z ? z->lookup(c.args[2]) : nullptr;
    if (!node) return c.reply.nil();
    c.reply.dbl(node->score);
}

void Store::cmd_zcard(Ctx &c) {
    ZSet *z;
    if (!zset_for_read(c, c.args[1], &z)) return;
    c.reply.integer(z ? static_cast<int64_t>(z->size()) : 0);
}

void Store::cmd_zrank(Ctx &c) {
    ZSet *z;
    if (!zset_for_read(c, c.args[1], &z)) return;
    ZNode *node = z ? z->lookup(c.args[2]) : nullptr;
    if (!node) return c.reply.nil();
    c.reply.integer(ZSet::rank(node));
}

void Store::cmd_zrange(Ctx &c) {
    int64_t start = 0, stop = 0;
    if (!parse_int(c.args[2], &start) || !parse_int(c.args[3], &stop)) return c.reply.error(kNotInt);
    bool withscores = false;
    if (c.args.size() == 5 && ieq(c.args[4], "WITHSCORES")) {
        withscores = true;
    } else if (c.args.size() != 4) {
        return c.reply.error(kSyntax);
    }
    ZSet *z;
    if (!zset_for_read(c, c.args[1], &z)) return;
    int64_t n = z ? static_cast<int64_t>(z->size()) : 0;
    if (start < 0) start += n;
    if (stop < 0) stop += n;
    if (start < 0) start = 0;
    if (stop >= n) stop = n - 1;
    if (start > stop || start >= n) return c.reply.array(0);
    int64_t count = stop - start + 1;
    c.reply.array(static_cast<size_t>(withscores ? count * 2 : count));
    ZNode *node = z->at(start);  // O(log n) thanks to the subtree counts
    for (int64_t i = 0; i < count; i++, node = ZSet::offset(node, 1)) {
        c.reply.bulk(node->name);
        if (withscores) c.reply.dbl(node->score);
    }
}

void Store::cmd_zrangebyscore(Ctx &c) {
    double min = 0, max = 0;
    bool min_ex = false, max_ex = false;
    if (!parse_bound(c.args[2], &min, &min_ex) || !parse_bound(c.args[3], &max, &max_ex)) {
        return c.reply.error("ERR min or max is not a float");
    }
    bool withscores = false;
    int64_t offset = 0, limit = -1;
    for (size_t i = 4; i < c.args.size(); i++) {
        if (ieq(c.args[i], "WITHSCORES")) {
            withscores = true;
        } else if (ieq(c.args[i], "LIMIT") && i + 2 < c.args.size()) {
            if (!parse_int(c.args[i + 1], &offset) || !parse_int(c.args[i + 2], &limit)) return c.reply.error(kNotInt);
            i += 2;
        } else {
            return c.reply.error(kSyntax);
        }
    }
    ZSet *z;
    if (!zset_for_read(c, c.args[1], &z)) return;
    std::vector<ZNode *> found;
    if (z && offset >= 0) {
        ZNode *node = z->seek_ge(min, "");  // "" sorts before every name with that score
        while (node && min_ex && node->score == min) node = ZSet::offset(node, 1);
        node = ZSet::offset(node, offset);  // skipping is O(log n), not O(offset)
        while (node && (limit < 0 || static_cast<int64_t>(found.size()) < limit)) {
            if (max_ex ? node->score >= max : node->score > max) break;
            found.push_back(node);
            node = ZSet::offset(node, 1);
        }
    }
    c.reply.array(withscores ? found.size() * 2 : found.size());
    for (ZNode *node : found) {
        c.reply.bulk(node->name);
        if (withscores) c.reply.dbl(node->score);
    }
}

// The tutorial's own range query: ZQUERY key score name offset limit returns
// up to `limit` pairs starting `offset` places after the first (score, name)
// >= the given pair. A negative offset walks backwards.
void Store::cmd_zquery(Ctx &c) {
    double score = 0;
    if (!parse_double(c.args[2], &score)) return c.reply.error(kNotFloat);
    int64_t offset = 0, limit = 0;
    if (!parse_int(c.args[4], &offset) || !parse_int(c.args[5], &limit)) return c.reply.error(kNotInt);
    ZSet *z;
    if (!zset_for_read(c, c.args[1], &z)) return;
    std::vector<ZNode *> found;
    if (z && limit > 0) {
        ZNode *node = ZSet::offset(z->seek_ge(score, c.args[3]), offset);
        while (node && static_cast<int64_t>(found.size()) < limit) {
            found.push_back(node);
            node = ZSet::offset(node, 1);
        }
    }
    c.reply.array(found.size() * 2);
    for (ZNode *node : found) {
        c.reply.bulk(node->name);
        c.reply.dbl(node->score);
    }
}

// ---- glob -------------------------------------------------------------------

bool glob_match(std::string_view p, std::string_view s) {
    size_t pi = 0, si = 0;
    size_t star_p = std::string_view::npos, star_s = 0;  // backtrack point for '*'
    while (si < s.size()) {
        if (pi < p.size()) {
            char pc = p[pi];
            if (pc == '*') {
                star_p = ++pi;
                star_s = si;
                continue;
            }
            if (pc == '?') {
                pi++;
                si++;
                continue;
            }
            if (pc == '[') {
                size_t j = pi + 1;
                bool negate = j < p.size() && p[j] == '^';
                if (negate) j++;
                bool matched = false;
                bool first = true;
                while (j < p.size() && (p[j] != ']' || first)) {
                    first = false;
                    if (p[j] == '\\' && j + 1 < p.size()) {
                        j++;
                        matched |= p[j] == s[si];
                        j++;
                    } else if (j + 2 < p.size() && p[j + 1] == '-' && p[j + 2] != ']') {
                        char lo = std::min(p[j], p[j + 2]), hi = std::max(p[j], p[j + 2]);
                        matched |= s[si] >= lo && s[si] <= hi;
                        j += 3;
                    } else {
                        matched |= p[j] == s[si];
                        j++;
                    }
                }
                if (j < p.size()) {  // found the closing ']'
                    if (matched != negate) {
                        pi = j + 1;
                        si++;
                        continue;
                    }
                } else if (p[pi] == s[si]) {  // unterminated '[' matches itself
                    pi++;
                    si++;
                    continue;
                }
            } else {
                if (pc == '\\' && pi + 1 < p.size()) pc = p[++pi];
                if (pc == s[si]) {
                    pi++;
                    si++;
                    continue;
                }
            }
        }
        if (star_p == std::string_view::npos) return false;
        pi = star_p;  // let the last '*' swallow one more character
        si = ++star_s;
    }
    while (pi < p.size() && p[pi] == '*') pi++;
    return pi == p.size();
}
