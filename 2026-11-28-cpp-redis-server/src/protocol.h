// Wire protocol. All integers are little-endian.
//
// Request:   u32 total_len | u32 nstr | nstr x (u32 len | bytes)
// Response:  u32 total_len | one serialized value
// Value:     u8 tag, then
//              NIL  (0)  nothing
//              ERR  (1)  u32 code | u32 len | message bytes
//              STR  (2)  u32 len | bytes
//              INT  (3)  i64
//              DBL  (4)  f64 (IEEE 754 bits)
//              ARR  (5)  u32 n | n values
//
// The length prefix is what lets the server find message boundaries in a TCP
// byte stream, which has none of its own. The tag-length-value encoding lets a
// client decode any reply without knowing which command produced it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

constexpr size_t k_max_msg = 32u << 20;  // 32 MiB per message, either direction
constexpr size_t k_max_args = 200 * 1000;

enum Tag : uint8_t { TAG_NIL = 0, TAG_ERR = 1, TAG_STR = 2, TAG_INT = 3, TAG_DBL = 4, TAG_ARR = 5 };
enum ErrCode : uint32_t { ERR_UNKNOWN = 1, ERR_TOO_BIG = 2, ERR_BAD_TYP = 3, ERR_BAD_ARG = 4 };

void put_u32(std::string &out, uint32_t v);
void patch_u32(std::string &out, size_t pos, uint32_t v);
bool get_u32(const uint8_t *&p, const uint8_t *end, uint32_t &v);

// Whole framed request, length prefix included.
std::string encode_request(const std::vector<std::string> &args);
// Request body (after the length prefix). Fails on truncation or trailing bytes.
bool parse_request(const uint8_t *data, size_t len, std::vector<std::string> &out);

void out_nil(std::string &out);
void out_str(std::string &out, std::string_view s);
void out_int(std::string &out, int64_t v);
void out_dbl(std::string &out, double v);
void out_err(std::string &out, uint32_t code, std::string_view msg);
void out_arr(std::string &out, uint32_t n);
// For arrays whose length is only known after writing the elements.
size_t out_begin_arr(std::string &out);
void out_end_arr(std::string &out, size_t ctx, uint32_t n);

struct Value {
    Tag tag = TAG_NIL;
    int64_t i = 0;
    double d = 0;
    uint32_t code = 0;  // ERR only
    std::string s;      // STR, and ERR message
    std::vector<Value> arr;
};

bool decode_value(const uint8_t *&p, const uint8_t *end, Value &v, int depth = 0);
// Response body (after the length prefix). Fails on truncation or trailing bytes.
bool decode_response(const uint8_t *data, size_t len, Value &v);
// redis-cli style text, one line per value, e.g. "(int) 1".
std::string format_value(const Value &v);
