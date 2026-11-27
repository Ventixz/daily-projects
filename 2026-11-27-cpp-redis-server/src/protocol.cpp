#include "protocol.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static void put_u64(Buffer &out, uint64_t v) {
    for (int i = 0; i < 8; i++) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
static uint64_t get_u64(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = v << 8 | p[i];
    return v;
}
static void put_bytes(Buffer &out, std::string_view s) { out.insert(out.end(), s.begin(), s.end()); }
static void patch_u32(Buffer &out, size_t at, uint32_t v) {
    for (int i = 0; i < 4; i++) out[at + i] = static_cast<uint8_t>(v >> (8 * i));
}

void out_nil(Buffer &out) { out.push_back(TAG_NIL); }

void out_str(Buffer &out, std::string_view s) {
    out.push_back(TAG_STR);
    put_u32(out, static_cast<uint32_t>(s.size()));
    put_bytes(out, s);
}

void out_int(Buffer &out, int64_t v) {
    out.push_back(TAG_INT);
    put_u64(out, static_cast<uint64_t>(v));
}

void out_dbl(Buffer &out, double v) {
    out.push_back(TAG_DBL);
    uint64_t bits;
    memcpy(&bits, &v, sizeof bits);
    put_u64(out, bits);
}

void out_err(Buffer &out, uint32_t code, std::string_view msg) {
    out.push_back(TAG_ERR);
    put_u32(out, code);
    put_u32(out, static_cast<uint32_t>(msg.size()));
    put_bytes(out, msg);
}

void out_arr(Buffer &out, uint32_t n) {
    out.push_back(TAG_ARR);
    put_u32(out, n);
}

size_t out_begin_arr(Buffer &out) {
    out.push_back(TAG_ARR);
    put_u32(out, 0);
    return out.size() - 4;
}

void out_end_arr(Buffer &out, size_t ctx, uint32_t n) {
    patch_u32(out, ctx, n);
}

size_t response_begin(Buffer &out) {
    size_t header = out.size();
    put_u32(out, 0);
    return header;
}

void response_end(Buffer &out, size_t header) {
    size_t len = out.size() - header - 4;
    if (len > k_max_msg) {
        out.resize(header + 4);
        out_err(out, ERR_TOO_BIG, "response is too big");
        len = out.size() - header - 4;
    }
    patch_u32(out, header, static_cast<uint32_t>(len));
}

Parse parse_request(const uint8_t *data, size_t len, std::vector<std::string> &args,
                    size_t &consumed) {
    if (len < 4) return Parse::Incomplete;
    uint32_t msglen = get_u32(data);
    if (msglen > k_max_msg) return Parse::Error;
    if (len < 4 + size_t(msglen)) return Parse::Incomplete;

    const uint8_t *p = data + 4;
    const uint8_t *end = p + msglen;
    if (end - p < 4) return Parse::Error;
    uint32_t nstr = get_u32(p);
    p += 4;
    if (nstr > k_max_args) return Parse::Error;
    args.clear();
    for (uint32_t i = 0; i < nstr; i++) {
        if (end - p < 4) return Parse::Error;
        uint32_t slen = get_u32(p);
        p += 4;
        if (size_t(end - p) < slen) return Parse::Error;
        args.emplace_back(reinterpret_cast<const char *>(p), slen);
        p += slen;
    }
    if (p != end) return Parse::Error;  // trailing garbage
    consumed = 4 + size_t(msglen);
    return Parse::Ok;
}

void encode_request(Buffer &out, const std::vector<std::string> &args) {
    size_t header = out.size();
    put_u32(out, 0);
    put_u32(out, static_cast<uint32_t>(args.size()));
    for (const auto &a : args) {
        put_u32(out, static_cast<uint32_t>(a.size()));
        put_bytes(out, a);
    }
    patch_u32(out, header, static_cast<uint32_t>(out.size() - header - 4));
}

// Decodes one value from [p, end). Returns false on malformed input.
static bool parse_value(const uint8_t *&p, const uint8_t *end, Value &v, size_t depth) {
    if (depth > k_max_depth || p == end) return false;
    v = Value{};
    v.tag = static_cast<Tag>(*p++);
    size_t avail = size_t(end - p);
    switch (v.tag) {
    case TAG_NIL:
        return true;
    case TAG_ERR: {
        if (avail < 8) return false;
        v.code = get_u32(p);
        uint32_t n = get_u32(p + 4);
        p += 8;
        if (size_t(end - p) < n) return false;
        v.s.assign(reinterpret_cast<const char *>(p), n);
        p += n;
        return true;
    }
    case TAG_STR: {
        if (avail < 4) return false;
        uint32_t n = get_u32(p);
        p += 4;
        if (size_t(end - p) < n) return false;
        v.s.assign(reinterpret_cast<const char *>(p), n);
        p += n;
        return true;
    }
    case TAG_INT:
        if (avail < 8) return false;
        v.i = static_cast<int64_t>(get_u64(p));
        p += 8;
        return true;
    case TAG_DBL: {
        if (avail < 8) return false;
        uint64_t bits = get_u64(p);
        memcpy(&v.d, &bits, sizeof bits);
        p += 8;
        return true;
    }
    case TAG_ARR: {
        if (avail < 4) return false;
        uint32_t n = get_u32(p);
        p += 4;
        // Each element is at least one byte, so a huge n cannot be honest.
        if (size_t(end - p) < n) return false;
        v.arr.resize(n);
        for (uint32_t i = 0; i < n; i++) {
            if (!parse_value(p, end, v.arr[i], depth + 1)) return false;
        }
        return true;
    }
    }
    return false;  // unknown tag
}

Parse parse_response(const uint8_t *data, size_t len, Value &v, size_t &consumed) {
    if (len < 4) return Parse::Incomplete;
    uint32_t msglen = get_u32(data);
    if (msglen > k_max_msg) return Parse::Error;
    if (len < 4 + size_t(msglen)) return Parse::Incomplete;
    const uint8_t *p = data + 4;
    const uint8_t *end = p + msglen;
    if (!parse_value(p, end, v, 0) || p != end) return Parse::Error;
    consumed = 4 + size_t(msglen);
    return Parse::Ok;
}

static void format_into(const Value &v, std::string &out) {
    char buf[64];
    switch (v.tag) {
    case TAG_NIL:
        out += "(nil)\n";
        break;
    case TAG_ERR:
        snprintf(buf, sizeof buf, "(err) %u ", v.code);
        out += buf;
        out += v.s;
        out += '\n';
        break;
    case TAG_STR:
        out += "(str) ";
        out += v.s;
        out += '\n';
        break;
    case TAG_INT:
        snprintf(buf, sizeof buf, "(int) %lld\n", static_cast<long long>(v.i));
        out += buf;
        break;
    case TAG_DBL:
        // Shortest decimal form that reads back as the same double.
        for (int prec = 1; prec <= 17; prec++) {
            snprintf(buf, sizeof buf, "%.*g", prec, v.d);
            if (strtod(buf, nullptr) == v.d || std::isnan(v.d)) break;
        }
        out += "(dbl) ";
        out += buf;
        out += '\n';
        break;
    case TAG_ARR:
        snprintf(buf, sizeof buf, "(arr) len=%zu\n", v.arr.size());
        out += buf;
        for (const auto &e : v.arr) format_into(e, out);
        out += "(arr) end\n";
        break;
    }
}

std::string format_value(const Value &v) {
    std::string out;
    format_into(v, out);
    return out;
}
