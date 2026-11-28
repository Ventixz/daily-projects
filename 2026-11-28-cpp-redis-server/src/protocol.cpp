#include "protocol.h"

#include <charconv>
#include <cstring>

namespace {

void put_u64(std::string &out, uint64_t v) {
    for (int i = 0; i < 8; i++) out.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
}

bool get_u64(const uint8_t *&p, const uint8_t *end, uint64_t &v) {
    if (end - p < 8) return false;
    v = 0;
    for (int i = 0; i < 8; i++) v |= uint64_t(p[i]) << (8 * i);
    p += 8;
    return true;
}

constexpr int k_max_depth = 64;

}  // namespace

void put_u32(std::string &out, uint32_t v) {
    for (int i = 0; i < 4; i++) out.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
}

void patch_u32(std::string &out, size_t pos, uint32_t v) {
    for (int i = 0; i < 4; i++) out[pos + i] = static_cast<char>((v >> (8 * i)) & 0xff);
}

bool get_u32(const uint8_t *&p, const uint8_t *end, uint32_t &v) {
    if (end - p < 4) return false;
    v = uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
    p += 4;
    return true;
}

std::string encode_request(const std::vector<std::string> &args) {
    std::string out;
    put_u32(out, 0);  // patched below
    put_u32(out, static_cast<uint32_t>(args.size()));
    for (const std::string &a : args) {
        put_u32(out, static_cast<uint32_t>(a.size()));
        out += a;
    }
    patch_u32(out, 0, static_cast<uint32_t>(out.size() - 4));
    return out;
}

bool parse_request(const uint8_t *data, size_t len, std::vector<std::string> &out) {
    const uint8_t *p = data;
    const uint8_t *end = data + len;
    uint32_t nstr = 0;
    if (!get_u32(p, end, nstr) || nstr > k_max_args) return false;
    out.clear();
    out.reserve(nstr);
    while (out.size() < nstr) {
        uint32_t slen = 0;
        if (!get_u32(p, end, slen) || uint64_t(end - p) < slen) return false;
        out.emplace_back(reinterpret_cast<const char *>(p), slen);
        p += slen;
    }
    return p == end;
}

void out_nil(std::string &out) { out.push_back(static_cast<char>(TAG_NIL)); }

void out_str(std::string &out, std::string_view s) {
    out.push_back(static_cast<char>(TAG_STR));
    put_u32(out, static_cast<uint32_t>(s.size()));
    out.append(s.data(), s.size());
}

void out_int(std::string &out, int64_t v) {
    out.push_back(static_cast<char>(TAG_INT));
    put_u64(out, static_cast<uint64_t>(v));
}

void out_dbl(std::string &out, double v) {
    out.push_back(static_cast<char>(TAG_DBL));
    uint64_t bits;
    std::memcpy(&bits, &v, sizeof bits);
    put_u64(out, bits);
}

void out_err(std::string &out, uint32_t code, std::string_view msg) {
    out.push_back(static_cast<char>(TAG_ERR));
    put_u32(out, code);
    put_u32(out, static_cast<uint32_t>(msg.size()));
    out.append(msg.data(), msg.size());
}

void out_arr(std::string &out, uint32_t n) {
    out.push_back(static_cast<char>(TAG_ARR));
    put_u32(out, n);
}

size_t out_begin_arr(std::string &out) {
    out.push_back(static_cast<char>(TAG_ARR));
    put_u32(out, 0);
    return out.size() - 4;
}

void out_end_arr(std::string &out, size_t ctx, uint32_t n) { patch_u32(out, ctx, n); }

bool decode_value(const uint8_t *&p, const uint8_t *end, Value &v, int depth) {
    if (p == end || depth > k_max_depth) return false;
    v = Value{};
    uint8_t tag = *p++;
    switch (tag) {
    case TAG_NIL:
        v.tag = TAG_NIL;
        return true;
    case TAG_ERR: {
        uint32_t len = 0;
        if (!get_u32(p, end, v.code) || !get_u32(p, end, len) || uint64_t(end - p) < len) return false;
        v.tag = TAG_ERR;
        v.s.assign(reinterpret_cast<const char *>(p), len);
        p += len;
        return true;
    }
    case TAG_STR: {
        uint32_t len = 0;
        if (!get_u32(p, end, len) || uint64_t(end - p) < len) return false;
        v.tag = TAG_STR;
        v.s.assign(reinterpret_cast<const char *>(p), len);
        p += len;
        return true;
    }
    case TAG_INT: {
        uint64_t u = 0;
        if (!get_u64(p, end, u)) return false;
        v.tag = TAG_INT;
        v.i = static_cast<int64_t>(u);
        return true;
    }
    case TAG_DBL: {
        uint64_t u = 0;
        if (!get_u64(p, end, u)) return false;
        v.tag = TAG_DBL;
        std::memcpy(&v.d, &u, sizeof u);
        return true;
    }
    case TAG_ARR: {
        uint32_t n = 0;
        if (!get_u32(p, end, n)) return false;
        // Every element needs at least one byte, so a count larger than the
        // remaining input is a lie; reject it before reserving memory for it.
        if (n > uint64_t(end - p)) return false;
        v.tag = TAG_ARR;
        v.arr.resize(n);
        for (Value &elem : v.arr) {
            if (!decode_value(p, end, elem, depth + 1)) return false;
        }
        return true;
    }
    default:
        return false;
    }
}

bool decode_response(const uint8_t *data, size_t len, Value &v) {
    const uint8_t *p = data;
    return decode_value(p, data + len, v) && p == data + len;
}

std::string format_value(const Value &v) {
    switch (v.tag) {
    case TAG_NIL:
        return "(nil)\n";
    case TAG_ERR:
        return "(err) " + std::to_string(v.code) + " " + v.s + "\n";
    case TAG_STR:
        return "(str) " + v.s + "\n";
    case TAG_INT:
        return "(int) " + std::to_string(v.i) + "\n";
    case TAG_DBL: {
        char buf[64];
        auto res = std::to_chars(buf, buf + sizeof buf, v.d);  // shortest round-trip form
        return "(dbl) " + std::string(buf, res.ptr) + "\n";
    }
    case TAG_ARR: {
        std::string s = "(arr) len=" + std::to_string(v.arr.size()) + "\n";
        for (const Value &e : v.arr) s += format_value(e);
        return s + "(arr) end\n";
    }
    }
    return "(unknown)\n";
}
