// RESP2, the Redis wire protocol: an incremental request parser and a reply
// writer.
//
// The tutorial designs its own length-prefixed binary protocol. This project
// speaks real RESP instead, so the stock `redis-cli` and `redis-benchmark`
// work against it, and its replies can be compared byte for byte with a real
// Redis server in the tests.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

enum class ParseResult { kOk, kIncomplete, kError };

constexpr size_t kMaxArgs = 1024 * 1024;
constexpr size_t kMaxBulkLen = 64 * 1024 * 1024;
constexpr size_t kMaxInlineLen = 64 * 1024;

// Parses one request from the front of `buf`. Accepts both multibulk form
// (*2\r\n$3\r\nGET\r\n$1\r\nk\r\n) and inline form ("GET k\r\n", what you type
// into telnet or nc). On kOk, `args` holds the request and `*consumed` how
// many bytes it used. On kIncomplete nothing is consumed: call again once more
// bytes have arrived. On kError `*err` explains why.
ParseResult parse_request(std::string_view buf, std::vector<std::string> &args, size_t *consumed,
                          std::string *err);

// Appends RESP replies to a string.
class Reply {
public:
    explicit Reply(std::string &out) : out_(out) {}
    void simple(std::string_view s);  // +OK
    void error(std::string_view s);   // -ERR ...
    void integer(int64_t v);          // :42
    void bulk(std::string_view s);    // $5\r\nhello
    void nil();                       // $-1
    void array(size_t n);             // *n, followed by n replies
    void dbl(double v);               // RESP2 has no double type: a bulk string

private:
    std::string &out_;
};

// Shortest text that reads back as the same double ("1.5", "inf", "-0").
std::string format_double(double v);
