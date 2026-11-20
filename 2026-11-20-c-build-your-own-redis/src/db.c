#include "db.h"
#include "common.h"
#include "zset.h"

#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

enum { T_STR = 1, T_ZSET = 2 };

typedef struct {
    HNode node;
    int type;
    size_t heap_idx;  /* slot in db->ttl, or HEAP_NONE */
    char *val;        /* T_STR */
    size_t vlen;
    ZSet zset;        /* T_ZSET */
    size_t klen;
    char key[];
} Entry;

typedef struct {
    HNode node;
    const char *key;
    size_t klen;
} LookupKey;

static bool entry_eq(const HNode *node, const HNode *lookup) {
    const Entry *e = container_of(node, Entry, node);
    const LookupKey *k = container_of(lookup, LookupKey, node);
    return e->klen == k->klen && memcmp(e->key, k->key, e->klen) == 0;
}

static LookupKey make_key(const Str *s) {
    return (LookupKey){
        .node = {.next = NULL, .hcode = str_hash((const uint8_t *)s->ptr, s->len)},
        .key = s->ptr,
        .klen = s->len,
    };
}

static void *xmalloc(size_t n) {
    void *p = malloc(n);
    if (!p) { perror("malloc"); abort(); }
    return p;
}

static Entry *entry_new(const Str *key, int type) {
    Entry *e = xmalloc(sizeof(Entry) + key->len);
    memset(e, 0, sizeof *e);
    e->node.hcode = str_hash((const uint8_t *)key->ptr, key->len);
    e->type = type;
    e->heap_idx = HEAP_NONE;
    e->klen = key->len;
    memcpy(e->key, key->ptr, key->len);
    return e;
}

static void entry_clear_value(Entry *e) {
    if (e->type == T_STR) free(e->val);
    else if (e->type == T_ZSET) zset_clear(&e->zset);
    e->val = NULL;
    e->vlen = 0;
}

static void entry_destroy(DB *db, Entry *e) {
    heap_remove(&db->ttl, &e->heap_idx);
    entry_clear_value(e);
    free(e);
}

static void entry_set_ttl(DB *db, Entry *e, uint64_t now_ms, int64_t ttl_ms) {
    if (ttl_ms < 0) heap_remove(&db->ttl, &e->heap_idx);
    else heap_upsert(&db->ttl, &e->heap_idx, now_ms + (uint64_t)ttl_ms);
}

static void delete_entry(DB *db, Entry *e) {
    LookupKey k = {.node = {.next = NULL, .hcode = e->node.hcode}, .key = e->key, .klen = e->klen};
    hm_delete(&db->keys, &k.node, entry_eq);
    entry_destroy(db, e);
}

/* Finds a live key. An expired key that the timer has not reaped yet is
 * deleted here (lazy expiry), so no command can ever observe it. */
static Entry *lookup(DB *db, const Str *key, uint64_t now_ms) {
    LookupKey k = make_key(key);
    HNode *node = hm_lookup(&db->keys, &k.node, entry_eq);
    if (!node) return NULL;
    Entry *e = container_of(node, Entry, node);
    if (e->heap_idx != HEAP_NONE && db->ttl.items[e->heap_idx].val <= now_ms) {
        delete_entry(db, e);
        return NULL;
    }
    return e;
}

/* --- argument parsing ------------------------------------------------- */

static bool str_eq(const Str *s, const char *lit) {
    size_t n = strlen(lit);
    if (s->len != n) return false;
    for (size_t i = 0; i < n; i++) {
        char c = s->ptr[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != lit[i]) return false;
    }
    return true;
}

/* Arguments are not NUL-terminated, so copy into a small buffer first. */
static bool parse_int(const Str *s, int64_t *out) {
    char buf[32];
    if (s->len == 0 || s->len >= sizeof buf) return false;
    memcpy(buf, s->ptr, s->len);
    buf[s->len] = '\0';
    char *end;
    errno = 0;
    long long v = strtoll(buf, &end, 10);
    if (errno || *end != '\0') return false;
    *out = v;
    return true;
}

static bool parse_dbl(const Str *s, double *out) {
    char buf[64];
    if (s->len == 0 || s->len >= sizeof buf) return false;
    memcpy(buf, s->ptr, s->len);
    buf[s->len] = '\0';
    char *end;
    errno = 0;
    double v = strtod(buf, &end);
    /* NaN has no place in an ordering; overflow to inf is an error too. */
    if (errno || *end != '\0' || isnan(v)) return false;
    *out = v;
    return true;
}

/* --- commands --------------------------------------------------------- */

static void do_get(DB *db, const Strs *cmd, Buf *out, uint64_t now) {
    Entry *e = lookup(db, &cmd->v[1], now);
    if (!e) { out_nil(out); return; }
    if (e->type != T_STR) { out_err(out, ERR_BAD_TYP, "not a string value"); return; }
    out_str(out, e->val, e->vlen);
}

static void do_set(DB *db, const Strs *cmd, Buf *out, uint64_t now) {
    const Str *key = &cmd->v[1], *val = &cmd->v[2];
    Entry *e = lookup(db, key, now);
    if (e) {
        /* Like Redis: SET replaces any type and discards the old TTL. */
        entry_clear_value(e);
        heap_remove(&db->ttl, &e->heap_idx);
    } else {
        e = entry_new(key, T_STR);
        hm_insert(&db->keys, &e->node);
    }
    e->type = T_STR;
    e->val = xmalloc(val->len ? val->len : 1);
    memcpy(e->val, val->ptr, val->len);
    e->vlen = val->len;
    out_nil(out);
}

static void do_del(DB *db, const Strs *cmd, Buf *out, uint64_t now) {
    Entry *e = lookup(db, &cmd->v[1], now);
    if (e) delete_entry(db, e);
    out_int(out, e ? 1 : 0);
}

typedef struct {
    Buf *out;
    uint64_t now;
    const Heap *ttl;
    uint32_t n;
} KeysCtx;

static bool keys_cb(HNode *node, void *arg) {
    KeysCtx *ctx = arg;
    Entry *e = container_of(node, Entry, node);
    if (e->heap_idx != HEAP_NONE && ctx->ttl->items[e->heap_idx].val <= ctx->now) return true;
    out_str(ctx->out, e->key, e->klen);
    ctx->n++;
    return true;
}

static void do_keys(DB *db, const Strs *cmd, Buf *out, uint64_t now) {
    (void)cmd;
    KeysCtx ctx = {.out = out, .now = now, .ttl = &db->ttl, .n = 0};
    size_t token = out_begin_arr(out);
    hm_foreach(&db->keys, keys_cb, &ctx);
    out_end_arr(out, token, ctx.n);
}

static void do_pexpire(DB *db, const Strs *cmd, Buf *out, uint64_t now) {
    int64_t ms;
    if (!parse_int(&cmd->v[2], &ms)) { out_err(out, ERR_BAD_ARG, "expect int64"); return; }
    Entry *e = lookup(db, &cmd->v[1], now);
    if (!e) { out_int(out, 0); return; }
    if (ms <= 0) delete_entry(db, e);  /* an expiry in the past means "gone now" */
    else entry_set_ttl(db, e, now, ms);
    out_int(out, 1);
}

static void do_pttl(DB *db, const Strs *cmd, Buf *out, uint64_t now) {
    Entry *e = lookup(db, &cmd->v[1], now);
    if (!e) { out_int(out, -2); return; }
    if (e->heap_idx == HEAP_NONE) { out_int(out, -1); return; }
    out_int(out, (int64_t)(db->ttl.items[e->heap_idx].val - now));
}

static void do_persist(DB *db, const Strs *cmd, Buf *out, uint64_t now) {
    Entry *e = lookup(db, &cmd->v[1], now);
    bool had = e && e->heap_idx != HEAP_NONE;
    if (had) heap_remove(&db->ttl, &e->heap_idx);
    out_int(out, had ? 1 : 0);
}

/* Returns the zset for key; NULL with an error written if it's another type.
 * A missing key is reported through *missing so callers can pick their reply. */
static ZSet *expect_zset(DB *db, const Str *key, Buf *out, uint64_t now, bool *missing) {
    Entry *e = lookup(db, key, now);
    *missing = !e;
    if (!e) return NULL;
    if (e->type != T_ZSET) {
        out_err(out, ERR_BAD_TYP, "expect zset");
        return NULL;
    }
    return &e->zset;
}

static void do_zadd(DB *db, const Strs *cmd, Buf *out, uint64_t now) {
    double score;
    if (!parse_dbl(&cmd->v[2], &score)) { out_err(out, ERR_BAD_ARG, "expect float"); return; }
    const Str *key = &cmd->v[1], *name = &cmd->v[3];
    bool missing;
    ZSet *zset = expect_zset(db, key, out, now, &missing);
    if (!zset && !missing) return;
    if (missing) {
        Entry *e = entry_new(key, T_ZSET);
        hm_insert(&db->keys, &e->node);
        zset = &e->zset;
    }
    out_int(out, zset_insert(zset, name->ptr, name->len, score) ? 1 : 0);
}

static void do_zrem(DB *db, const Strs *cmd, Buf *out, uint64_t now) {
    bool missing;
    ZSet *zset = expect_zset(db, &cmd->v[1], out, now, &missing);
    if (!zset) { if (missing) out_int(out, 0); return; }
    ZNode *node = zset_lookup(zset, cmd->v[2].ptr, cmd->v[2].len);
    if (node) zset_delete(zset, node);
    /* An empty sorted set is not kept around, same as Redis. */
    if (zset_size(zset) == 0) delete_entry(db, container_of(zset, Entry, zset));
    out_int(out, node ? 1 : 0);
}

static void do_zscore(DB *db, const Strs *cmd, Buf *out, uint64_t now) {
    bool missing;
    ZSet *zset = expect_zset(db, &cmd->v[1], out, now, &missing);
    if (!zset) { if (missing) out_nil(out); return; }
    ZNode *node = zset_lookup(zset, cmd->v[2].ptr, cmd->v[2].len);
    if (node) out_dbl(out, node->score);
    else out_nil(out);
}

static void do_zrank(DB *db, const Strs *cmd, Buf *out, uint64_t now) {
    bool missing;
    ZSet *zset = expect_zset(db, &cmd->v[1], out, now, &missing);
    if (!zset) { if (missing) out_nil(out); return; }
    ZNode *node = zset_lookup(zset, cmd->v[2].ptr, cmd->v[2].len);
    if (node) out_int(out, znode_rank(node));
    else out_nil(out);
}

static void do_zcard(DB *db, const Strs *cmd, Buf *out, uint64_t now) {
    bool missing;
    ZSet *zset = expect_zset(db, &cmd->v[1], out, now, &missing);
    if (!zset) { if (missing) out_int(out, 0); return; }
    out_int(out, (int64_t)zset_size(zset));
}

/* zquery key score name offset limit
 * Seek to the first member >= (score, name), skip `offset` members (may be
 * negative), then return up to `limit` (name, score) pairs. This one primitive
 * covers range-by-score, range-by-rank and pagination. */
static void do_zquery(DB *db, const Strs *cmd, Buf *out, uint64_t now) {
    double score;
    int64_t offset, limit;
    if (!parse_dbl(&cmd->v[2], &score)) { out_err(out, ERR_BAD_ARG, "expect float"); return; }
    if (!parse_int(&cmd->v[4], &offset) || !parse_int(&cmd->v[5], &limit)) {
        { out_err(out, ERR_BAD_ARG, "expect int64"); return; }
    }
    bool missing;
    ZSet *zset = expect_zset(db, &cmd->v[1], out, now, &missing);
    if (!zset && !missing) return;
    size_t token = out_begin_arr(out);
    uint32_t n = 0;
    if (zset && limit > 0) {
        const Str *name = &cmd->v[3];
        ZNode *node = zset_seekge(zset, score, name->ptr, name->len);
        node = znode_offset(node, offset);
        while (node && (int64_t)n < limit * 2) {
            out_str(out, node->name, node->len);
            out_dbl(out, node->score);
            n += 2;
            node = znode_offset(node, 1);
        }
    }
    out_end_arr(out, token, n);
}

typedef struct {
    const char *name;
    size_t nargs;  /* including the command name */
    void (*fn)(DB *, const Strs *, Buf *, uint64_t);
} Command;

static const Command COMMANDS[] = {
    {"get", 2, do_get},         {"set", 3, do_set},       {"del", 2, do_del},
    {"keys", 1, do_keys},       {"pexpire", 3, do_pexpire}, {"pttl", 2, do_pttl},
    {"persist", 2, do_persist}, {"zadd", 4, do_zadd},     {"zrem", 3, do_zrem},
    {"zscore", 3, do_zscore},   {"zrank", 3, do_zrank},   {"zcard", 2, do_zcard},
    {"zquery", 6, do_zquery},
};

void db_execute(DB *db, const Strs *cmd, Buf *out, uint64_t now_ms) {
    if (cmd->n == 0) { out_err(out, ERR_UNKNOWN, "empty command"); return; }
    for (size_t i = 0; i < sizeof COMMANDS / sizeof COMMANDS[0]; i++) {
        if (!str_eq(&cmd->v[0], COMMANDS[i].name)) continue;
        if (cmd->n != COMMANDS[i].nargs) { out_err(out, ERR_BAD_ARG, "wrong number of arguments"); return; }
        { COMMANDS[i].fn(db, cmd, out, now_ms); return; }
    }
    out_err(out, ERR_UNKNOWN, "unknown command");
}

void db_init(DB *db) { *db = (DB){0}; }

size_t db_size(DB *db) { return hm_size(&db->keys); }

uint64_t db_next_expiry(const DB *db) {
    const HeapItem *top = heap_top(&db->ttl);
    return top ? top->val : UINT64_MAX;
}

size_t db_expire(DB *db, uint64_t now_ms, size_t max_work) {
    size_t n = 0;
    const HeapItem *top;
    while (n < max_work && (top = heap_top(&db->ttl)) && top->val <= now_ms) {
        /* The heap stores &entry->heap_idx; step back to the Entry. */
        Entry *e = container_of(top->ref, Entry, heap_idx);
        delete_entry(db, e);
        n++;
    }
    return n;
}

static bool collect_cb(HNode *node, void *arg) {
    Entry ***cursor = arg;
    *(*cursor)++ = container_of(node, Entry, node);
    return true;
}

void db_free(DB *db) {
    size_t n = hm_size(&db->keys);
    Entry **all = n ? xmalloc(n * sizeof(Entry *)) : NULL;
    Entry **cursor = all;
    hm_foreach(&db->keys, collect_cb, &cursor);
    for (size_t i = 0; i < n; i++) {
        heap_remove(&db->ttl, &all[i]->heap_idx);
        entry_clear_value(all[i]);
        free(all[i]);
    }
    free(all);
    hm_clear(&db->keys);
    heap_free(&db->ttl);
}
