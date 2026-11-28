#include <string>
#include <vector>

#include "protocol.h"
#include "testing.h"

namespace {

const uint8_t *bytes(const std::string &s) { return reinterpret_cast<const uint8_t *>(s.data()); }

}  // namespace

TEST(protocol_request_layout_is_exact) {
    std::string req = encode_request({"get", "k"});
    // len=16 | nstr=2 | 3 "get" | 1 "k"
    std::string expect("\x10\0\0\0\x02\0\0\0\x03\0\0\0get\x01\0\0\0k", 20);
    CHECK(req == expect);
}

TEST(protocol_request_round_trip) {
    std::vector<std::string> args{"set", "", std::string("bin\0ary", 7), std::string(70000, 'x')};
    std::string req = encode_request(args);
    std::vector<std::string> got;
    CHECK(parse_request(bytes(req) + 4, req.size() - 4, got));
    CHECK(got == args);
}

TEST(protocol_request_rejects_malformed) {
    std::string req = encode_request({"set", "k", "v"});
    std::vector<std::string> got;
    const uint8_t *body = bytes(req) + 4;
    size_t len = req.size() - 4;
    for (size_t cut = 0; cut < len; cut++) CHECK(!parse_request(body, cut, got));  // truncated
    std::string extra = req.substr(4) + "!";
    CHECK(!parse_request(bytes(extra), extra.size(), got));  // trailing garbage
    std::string huge;
    put_u32(huge, uint32_t(k_max_args + 1));
    CHECK(!parse_request(bytes(huge), huge.size(), got));  // too many args
    std::string liar;
    put_u32(liar, 1);
    put_u32(liar, 0xffffffffu);  // string length beyond the buffer
    CHECK(!parse_request(bytes(liar), liar.size(), got));
}

TEST(protocol_value_round_trip_and_format) {
    std::string out;
    size_t ctx = out_begin_arr(out);
    out_nil(out);
    out_str(out, "hi");
    out_int(out, -42);
    out_dbl(out, 0.1);
    out_err(out, ERR_BAD_ARG, "nope");
    out_arr(out, 1);
    out_int(out, 7);
    out_end_arr(out, ctx, 6);

    Value v;
    CHECK(decode_response(bytes(out), out.size(), v));
    CHECK_EQ(v.arr.size(), size_t(6));
    CHECK_EQ(v.arr[2].i, int64_t(-42));
    CHECK_EQ(v.arr[3].d, 0.1);
    CHECK_EQ(v.arr[4].code, uint32_t(ERR_BAD_ARG));
    CHECK_EQ(format_value(v),
             std::string("(arr) len=6\n(nil)\n(str) hi\n(int) -42\n(dbl) 0.1\n"
                         "(err) 4 nope\n(arr) len=1\n(int) 7\n(arr) end\n(arr) end\n"));
}

TEST(protocol_decode_rejects_malformed) {
    std::string out;
    out_str(out, "hello");
    Value v;
    for (size_t cut = 0; cut < out.size(); cut++) CHECK(!decode_response(bytes(out), cut, v));
    std::string unknown("\x09", 1);
    CHECK(!decode_response(bytes(unknown), unknown.size(), v));
    std::string bomb;
    out_arr(bomb, 0xffffffffu);  // claims 4 billion elements in 5 bytes
    CHECK(!decode_response(bytes(bomb), bomb.size(), v));
    std::string deep;
    for (int i = 0; i < 100; i++) out_arr(deep, 1);
    out_nil(deep);
    CHECK(!decode_response(bytes(deep), deep.size(), v));  // nesting limit
}
