/* Tests for the wire protocol and every command, with the clock injected. */
#include "../src/common.h"
#include "../src/db.h"
#include "../src/proto.h"
#include "test.h"

#include <string.h>

static DB db;
static char reply[4096];

/* Splits `line` on spaces, runs it, and returns the printed reply. */
static const char *run_at(uint64_t now, const char *line) {
    char copy[512];
    snprintf(copy, sizeof copy, "%s", line);
    Str args[16];
    size_t n = 0;
    for (char *tok = strtok(copy, " "); tok && n < 16; tok = strtok(NULL, " ")) {
        args[n++] = (Str){tok, strlen(tok)};
    }
    /* Round-trip through the wire encoding so the parser is exercised too. */
    Buf req = {0};
    encode_request(&req, args, n);
    Strs cmd;
    int rv = parse_request(buf_data(&req) + 4, req.len - 4, &cmd);
    CHECK(rv == 0);
    CHECK(rd_u32(buf_data(&req)) == req.len - 4);

    Buf out = {0};
    db_execute(&db, &cmd, &out, now);
    free(cmd.v);
    buf_free(&req);

    FILE *f = fmemopen(reply, sizeof reply, "w");
    long used = print_value(f, buf_data(&out), out.len);
    fclose(f);
    CHECK(used >= 0 && (size_t)used == out.len);  /* exactly one value */
    buf_free(&out);
    /* Single-line replies: drop the trailing newline for easy comparison. */
    size_t len = strlen(reply);
    if (len && reply[len - 1] == '\n') reply[len - 1] = '\0';
    return reply;
}

static const char *run(const char *line) { return run_at(1000, line); }

#define EXPECT(line, want)                                                \
    do {                                                                  \
        const char *got_ = run(line);                                     \
        if (strcmp(got_, want) != 0) {                                    \
            fprintf(stderr, "  %s\n    got:  %s\n    want: %s\n", line, got_, want); \
        }                                                                 \
        CHECK(strcmp(got_, want) == 0);                                   \
    } while (0)

static void reset(void) {
    db_free(&db);
    db_init(&db);
}

static void test_parse_rejects_malformed(void) {
    Strs cmd;
    uint8_t short_body[3] = {1, 0, 0};
    CHECK(parse_request(short_body, sizeof short_body, &cmd) == -1);
    /* claims 1 string of length 10, provides 2 bytes */
    uint8_t truncated[] = {1, 0, 0, 0, 10, 0, 0, 0, 'h', 'i'};
    CHECK(parse_request(truncated, sizeof truncated, &cmd) == -1);
    /* valid single string followed by a stray byte */
    uint8_t trailing[] = {1, 0, 0, 0, 2, 0, 0, 0, 'h', 'i', '!'};
    CHECK(parse_request(trailing, sizeof trailing, &cmd) == -1);
    /* a huge count must be rejected without allocating for it */
    uint8_t huge[] = {0xff, 0xff, 0xff, 0x00};
    CHECK(parse_request(huge, sizeof huge, &cmd) == -1);
    uint8_t ok[] = {1, 0, 0, 0, 2, 0, 0, 0, 'h', 'i'};
    CHECK(parse_request(ok, sizeof ok, &cmd) == 0);
    CHECK(cmd.n == 1 && cmd.v[0].len == 2 && memcmp(cmd.v[0].ptr, "hi", 2) == 0);
    free(cmd.v);
}

static void test_strings(void) {
    reset();
    EXPECT("get k", "(nil)");
    EXPECT("set k v1", "(nil)");
    EXPECT("get k", "(str) v1");
    EXPECT("SET k v2", "(nil)");  /* command names are case-insensitive */
    EXPECT("GeT k", "(str) v2");
    EXPECT("del k", "(int) 1");
    EXPECT("del k", "(int) 0");
    EXPECT("get k", "(nil)");
}

static void test_errors(void) {
    reset();
    EXPECT("nope", "(err) 1 unknown command");
    EXPECT("get", "(err) 4 wrong number of arguments");
    EXPECT("set a b c", "(err) 4 wrong number of arguments");
    EXPECT("zadd z 1 m", "(int) 1");
    EXPECT("get z", "(err) 3 not a string value");
    EXPECT("set s v", "(nil)");
    EXPECT("zadd s 1 m", "(err) 3 expect zset");
    EXPECT("zadd z abc m", "(err) 4 expect float");
    EXPECT("zadd z nan m", "(err) 4 expect float");
    EXPECT("pexpire s 10x", "(err) 4 expect int64");
}

static void test_keys(void) {
    reset();
    EXPECT("keys", "(arr) len=0\n(arr) end");
    run("set a 1");
    run("zadd b 1 x");
    const char *got = run("keys");
    CHECK(strstr(got, "(arr) len=2") != NULL);
    CHECK(strstr(got, "(str) a\n") != NULL);
    CHECK(strstr(got, "(str) b\n") != NULL);
}

static void test_ttl_lazy_and_active_expiry(void) {
    reset();
    run_at(1000, "set a 1");
    CHECK(strcmp(run_at(1000, "pttl a"), "(int) -1") == 0);
    CHECK(strcmp(run_at(1000, "pttl missing"), "(int) -2") == 0);
    CHECK(strcmp(run_at(1000, "pexpire a 500"), "(int) 1") == 0);
    CHECK(strcmp(run_at(1200, "pttl a"), "(int) 300") == 0);
    CHECK(strcmp(run_at(1499, "get a"), "(str) 1") == 0);
    /* Lazy: past the deadline the key is invisible even before any reaping. */
    CHECK(strcmp(run_at(1500, "get a"), "(nil)") == 0);
    CHECK(db_size(&db) == 0);

    /* Active: db_expire reaps keys nobody asks about, in deadline order. */
    run_at(0, "set x 1");
    run_at(0, "set y 1");
    run_at(0, "set z 1");
    run_at(0, "pexpire x 30");
    run_at(0, "pexpire y 10");
    run_at(0, "pexpire z 20");
    CHECK(db_next_expiry(&db) == 10);
    CHECK(db_expire(&db, 15, 100) == 1);
    CHECK(strcmp(run_at(15, "get y"), "(nil)") == 0);
    CHECK(db_next_expiry(&db) == 20);
    CHECK(db_expire(&db, 100, 1) == 1);  /* work is bounded */
    CHECK(db_expire(&db, 100, 100) == 1);
    CHECK(db_size(&db) == 0);
    CHECK(db_next_expiry(&db) == UINT64_MAX);
}

static void test_ttl_reset_by_set_persist_and_negative(void) {
    reset();
    run_at(0, "set a 1");
    run_at(0, "pexpire a 100");
    run_at(10, "set a 2");  /* SET drops the TTL */
    CHECK(strcmp(run_at(500, "get a"), "(str) 2") == 0);
    run_at(500, "pexpire a 100");
    CHECK(strcmp(run_at(500, "persist a"), "(int) 1") == 0);
    CHECK(strcmp(run_at(500, "persist a"), "(int) 0") == 0);
    CHECK(strcmp(run_at(9999, "get a"), "(str) 2") == 0);
    CHECK(strcmp(run_at(9999, "pexpire a -1"), "(int) 1") == 0);  /* delete now */
    CHECK(strcmp(run_at(9999, "get a"), "(nil)") == 0);
    CHECK(strcmp(run_at(9999, "pexpire a 10"), "(int) 0") == 0);
    CHECK(db.ttl.len == 0);
}

static void test_zset_commands(void) {
    reset();
    EXPECT("zadd z 1 a", "(int) 1");
    EXPECT("zadd z 2 b", "(int) 1");
    EXPECT("zadd z 3 c", "(int) 1");
    EXPECT("zadd z 1.5 c", "(int) 0");  /* update */
    EXPECT("zscore z c", "(dbl) 1.5");
    EXPECT("zscore z nope", "(nil)");
    EXPECT("zscore missing a", "(nil)");
    EXPECT("zrank z a", "(int) 0");
    EXPECT("zrank z c", "(int) 1");
    EXPECT("zrank z b", "(int) 2");
    EXPECT("zcard z", "(int) 3");
    EXPECT("zquery z 0 x 0 10",
           "(arr) len=6\n(str) a\n(dbl) 1\n(str) c\n(dbl) 1.5\n(str) b\n(dbl) 2\n(arr) end");
    /* seek to >= (1.5, ""), then page: offset 1, limit 1 */
    EXPECT("zquery z 1.5 x 0 1", "(arr) len=2\n(str) b\n(dbl) 2\n(arr) end");
    EXPECT("zquery z 1.5 c 1 1", "(arr) len=2\n(str) b\n(dbl) 2\n(arr) end");
    EXPECT("zquery z 2 b -1 1", "(arr) len=2\n(str) c\n(dbl) 1.5\n(arr) end");
    EXPECT("zquery z 9 x 0 10", "(arr) len=0\n(arr) end");
    EXPECT("zquery z 0 x 0 0", "(arr) len=0\n(arr) end");
    EXPECT("zquery missing 0 x 0 10", "(arr) len=0\n(arr) end");
    EXPECT("zrem z a", "(int) 1");
    EXPECT("zrem z a", "(int) 0");
    EXPECT("zrem z b", "(int) 1");
    EXPECT("zrem z c", "(int) 1");
    EXPECT("get z", "(nil)");  /* emptied set is removed */
    EXPECT("zcard z", "(int) 0");
}

static void test_zset_with_ttl_and_overwrite(void) {
    reset();
    run_at(0, "zadd z 1 a");
    run_at(0, "pexpire z 50");
    CHECK(strcmp(run_at(49, "zcard z"), "(int) 1") == 0);
    CHECK(strcmp(run_at(50, "zcard z"), "(int) 0") == 0);
    run_at(60, "zadd z 1 a");
    run_at(60, "set z plain");  /* SET replaces a zset */
    CHECK(strcmp(run_at(60, "get z"), "(str) plain") == 0);
}

int main(void) {
    db_init(&db);
    RUN(test_parse_rejects_malformed);
    RUN(test_strings);
    RUN(test_errors);
    RUN(test_keys);
    RUN(test_ttl_lazy_and_active_expiry);
    RUN(test_ttl_reset_by_set_persist_and_negative);
    RUN(test_zset_commands);
    RUN(test_zset_with_ttl_and_overwrite);
    db_free(&db);
    DONE();
}
