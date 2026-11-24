#include "resp.h"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace {

// Reads "<digits>\r\n" starting at `pos`. Returns false on a malformed number;
// sets *complete=false if the line has not fully arrived yet.
bool read_len_line(std::string_view buf, size_t pos, int64_t *out, size_t *next, bool *complete) {
    size_t eol = buf.find("\r\n", pos);
    if (eol == std::string_view::npos) {
        *complete = false;
        // A length line is never longer than a few digits; anything else is junk.
        return buf.size() - pos <= 32;
    }
    *complete = true;
    const char *first = buf.data() + pos;
    const char *last = buf.data() + eol;
    if (first == last) return false;
    auto [p, ec] = std::from_chars(first, last, *out);
    if (ec != std::errc() || p != last) return false;
    *next = eol + 2;
    return true;
}

ParseResult parse_inline(std::string_view buf, std::vector<std::string> &args, size_t *consumed,
                         std::string *err) {
    size_t eol = buf.find('\n');
    if (eol == std::string_view::npos) {
        if (buf.size() > kMaxInlineLen) {
            *err = "Protocol error: too big inline request";
            return ParseResult::kError;
        }
        return ParseResult::kIncomplete;
    }
    std::string_view line = buf.substr(0, eol);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    args.clear();
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) i++;
        size_t start = i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t') i++;
        if (i > start) args.emplace_back(line.substr(start, i - start));
    }
    *consumed = eol + 1;
    return ParseResult::kOk;
}

}  // namespace

ParseResult parse_request(std::string_view buf, std::vector<std::string> &args, size_t *consumed,
                          std::string *err) {
    if (buf.empty()) return ParseResult::kIncomplete;
    if (buf[0] != '*') return parse_inline(buf, args, consumed, err);

    int64_t nargs = 0;
    size_t pos = 0;
    bool complete = false;
    if (!read_len_line(buf, 1, &nargs, &pos, &complete)) {
        *err = "Protocol error: invalid multibulk length";
        return ParseResult::kError;
    }
    if (!complete) return ParseResult::kIncomplete;
    if (nargs < 0 || static_cast<uint64_t>(nargs) > kMaxArgs) {
        *err = "Protocol error: invalid multibulk length";
        return ParseResult::kError;
    }

    // Validate and measure first; only build strings once the whole request is
    // here, so a request arriving one byte at a time costs O(n) copies, not O(n^2).
    std::vector<std::pair<size_t, size_t>> spans;
    // Don't trust the header for the allocation size: "*1048576\r\n" is 10 bytes.
    spans.reserve(std::min<size_t>(static_cast<size_t>(nargs), 1024));
    for (int64_t i = 0; i < nargs; i++) {
        if (pos >= buf.size()) return ParseResult::kIncomplete;
        if (buf[pos] != '$') {
            *err = std::string("Protocol error: expected '$', got '") + buf[pos] + "'";
            return ParseResult::kError;
        }
        int64_t len = 0;
        if (!read_len_line(buf, pos + 1, &len, &pos, &complete)) {
            *err = "Protocol error: invalid bulk length";
            return ParseResult::kError;
        }
        if (!complete) return ParseResult::kIncomplete;
        if (len < 0 || static_cast<uint64_t>(len) > kMaxBulkLen) {
            *err = "Protocol error: invalid bulk length";
            return ParseResult::kError;
        }
        size_t ulen = static_cast<size_t>(len);
        if (buf.size() - pos < ulen + 2) return ParseResult::kIncomplete;
        if (buf[pos + ulen] != '\r' || buf[pos + ulen + 1] != '\n') {
            *err = "Protocol error: bulk string not terminated by CRLF";
            return ParseResult::kError;
        }
        spans.emplace_back(pos, ulen);
        pos += ulen + 2;
    }
    args.clear();
    args.reserve(spans.size());
    for (auto [start, len] : spans) args.emplace_back(buf.substr(start, len));
    *consumed = pos;
    return ParseResult::kOk;
}

void Reply::simple(std::string_view s) {
    out_ += '+';
    out_ += s;
    out_ += "\r\n";
}

void Reply::error(std::string_view s) {
    out_ += '-';
    out_ += s;
    out_ += "\r\n";
}

void Reply::integer(int64_t v) {
    out_ += ':';
    out_ += std::to_string(v);
    out_ += "\r\n";
}

void Reply::bulk(std::string_view s) {
    out_ += '$';
    out_ += std::to_string(s.size());
    out_ += "\r\n";
    out_ += s;
    out_ += "\r\n";
}

void Reply::nil() { out_ += "$-1\r\n"; }

void Reply::array(size_t n) {
    out_ += '*';
    out_ += std::to_string(n);
    out_ += "\r\n";
}

void Reply::dbl(double v) { bulk(format_double(v)); }

std::string format_double(double v) {
    if (std::isinf(v)) return v > 0 ? "inf" : "-inf";
    char buf[64];
    auto [p, ec] = std::to_chars(buf, buf + sizeof(buf), v);  // shortest round-trip form
    return std::string(buf, p);
}
