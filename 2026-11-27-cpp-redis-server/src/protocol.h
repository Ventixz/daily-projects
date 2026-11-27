// Wire protocol. Everything is little-endian.
//
// Request frame:   u32 len | u32 nstr | (u32 len | bytes) * nstr
// Response frame:  u32 len | value
// value:           u8 tag, then
//     NIL  -
//     ERR  u32 code | u32 len | message
//     STR  u32 len | bytes
//     INT  i64
//     DBL  f64
//     ARR  u32 n | value * n
//
// Both sides have a length prefix, so a reader always knows whether it has a
// whole message before parsing it, and a client can send many requests
// without waiting (pipelining).
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

enum Tag : uint8_t { TAG_NIL = 0, TAG_ERR = 1, TAG_STR = 2, TAG_INT = 3, TAG_DBL = 4, TAG_ARR = 5 };
enum ErrCode : uint32_t { ERR_UNKNOWN = 1, ERR_TOO_BIG = 2, ERR_BAD_TYP = 3, ERR_BAD_ARG = 4 };

constexpr size_t k_max_msg = 32u << 20;  // 32 MiB, either direction
constexpr size_t k_max_args = 200 * 1000;
constexpr size_t k_max_depth = 64;  // nesting limit when decoding responses

using Buffer = std::vector<uint8_t>;

enum class Parse { Ok, Incomplete, Error };

// ---- building responses (server side) ----
void out_nil(Buffer &out);
void out_str(Buffer &out, std::string_view s);
void out_int(Buffer &out, int64_t v);
void out_dbl(Buffer &out, double v);
void out_err(Buffer &out, uint32_t code, std::string_view msg);
void out_arr(Buffer &out, uint32_t n);
// For arrays whose length is only known after writing the elements.
size_t out_begin_arr(Buffer &out);
void out_end_arr(Buffer &out, size_t ctx, uint32_t n);
// Reserve the length prefix, write the value, then patch the prefix. If the
// value grew past k_max_msg it is replaced with an ERR_TOO_BIG error.
size_t response_begin(Buffer &out);
void response_end(Buffer &out, size_t header);

// Parses one request frame from the front of `data`.
Parse parse_request(const uint8_t *data, size_t len, std::vector<std::string> &args,
                    size_t &consumed);

// ---- client side ----
void encode_request(Buffer &out, const std::vector<std::string> &args);

struct Value {
    Tag tag = TAG_NIL;
    int64_t i = 0;
    double d = 0;
    uint32_t code = 0;  // ERR
    std::string s;      // STR, or ERR message
    std::vector<Value> arr;
};

Parse parse_response(const uint8_t *data, size_t len, Value &v, size_t &consumed);
std::string format_value(const Value &v);  // human-readable, one item per line

// little-endian helpers
inline void put_u32(Buffer &out, uint32_t v) {
    for (int i = 0; i < 4; i++) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
inline uint32_t get_u32(const uint8_t *p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
