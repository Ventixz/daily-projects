#include "proto.h"
#include "common.h"

#include <inttypes.h>
#include <stdlib.h>

int parse_request(const uint8_t *data, size_t len, Strs *out) {
    out->v = NULL;
    out->n = 0;
    if (len < 4) return -1;
    uint32_t n = rd_u32(data);
    if (n > K_MAX_ARGS) return -1;
    /* Each string needs at least 4 bytes, so a huge count with a tiny body is
     * rejected before we allocate for it. */
    if ((size_t)n > (len - 4) / 4) return -1;
    Str *v = n ? malloc(n * sizeof(Str)) : NULL;
    if (n && !v) return -1;
    size_t pos = 4;
    for (uint32_t i = 0; i < n; i++) {
        if (len - pos < 4) { free(v); return -1; }
        uint32_t slen = rd_u32(data + pos);
        pos += 4;
        if (len - pos < slen) { free(v); return -1; }
        v[i].ptr = (const char *)data + pos;
        v[i].len = slen;
        pos += slen;
    }
    if (pos != len) { free(v); return -1; }  /* trailing garbage */
    out->v = v;
    out->n = n;
    return 0;
}

void encode_request(Buf *b, const Str *args, size_t n) {
    size_t body = 4;
    for (size_t i = 0; i < n; i++) body += 4 + args[i].len;
    buf_append_u32(b, (uint32_t)body);
    buf_append_u32(b, (uint32_t)n);
    for (size_t i = 0; i < n; i++) {
        buf_append_u32(b, (uint32_t)args[i].len);
        buf_append(b, args[i].ptr, args[i].len);
    }
}

void out_nil(Buf *b) { buf_append_u8(b, TAG_NIL); }

void out_str(Buf *b, const char *s, size_t len) {
    buf_append_u8(b, TAG_STR);
    buf_append_u32(b, (uint32_t)len);
    buf_append(b, s, len);
}

void out_int(Buf *b, int64_t v) {
    buf_append_u8(b, TAG_INT);
    buf_append_u64(b, (uint64_t)v);
}

void out_dbl(Buf *b, double v) {
    uint64_t bits;
    memcpy(&bits, &v, sizeof bits);
    buf_append_u8(b, TAG_DBL);
    buf_append_u64(b, bits);
}

void out_err(Buf *b, uint32_t code, const char *msg) {
    size_t len = strlen(msg);
    buf_append_u8(b, TAG_ERR);
    buf_append_u32(b, code);
    buf_append_u32(b, (uint32_t)len);
    buf_append(b, msg, len);
}

size_t out_begin_arr(Buf *b) {
    buf_append_u8(b, TAG_ARR);
    buf_append_u32(b, 0);
    return b->len - 4;
}

void out_end_arr(Buf *b, size_t token, uint32_t n) {
    uint8_t *p = buf_data(b) + token;
    if (p[-1] != TAG_ARR) abort();  /* token must come from out_begin_arr */
    wr_u32(p, n);
}

long print_value(FILE *f, const uint8_t *data, size_t len) {
    if (len < 1) return -1;
    switch (data[0]) {
    case TAG_NIL:
        fprintf(f, "(nil)\n");
        return 1;
    case TAG_ERR: {
        if (len < 9) return -1;
        uint32_t code = rd_u32(data + 1), mlen = rd_u32(data + 5);
        if (len - 9 < mlen) return -1;
        fprintf(f, "(err) %" PRIu32 " %.*s\n", code, (int)mlen, (const char *)data + 9);
        return 9 + (long)mlen;
    }
    case TAG_STR: {
        if (len < 5) return -1;
        uint32_t slen = rd_u32(data + 1);
        if (len - 5 < slen) return -1;
        fprintf(f, "(str) %.*s\n", (int)slen, (const char *)data + 5);
        return 5 + (long)slen;
    }
    case TAG_INT:
        if (len < 9) return -1;
        fprintf(f, "(int) %" PRId64 "\n", (int64_t)rd_u64(data + 1));
        return 9;
    case TAG_DBL: {
        if (len < 9) return -1;
        uint64_t bits = rd_u64(data + 1);
        double v;
        memcpy(&v, &bits, sizeof v);
        fprintf(f, "(dbl) %.17g\n", v);
        return 9;
    }
    case TAG_ARR: {
        if (len < 5) return -1;
        uint32_t n = rd_u32(data + 1);
        fprintf(f, "(arr) len=%" PRIu32 "\n", n);
        size_t pos = 5;
        for (uint32_t i = 0; i < n; i++) {
            long rv = print_value(f, data + pos, len - pos);
            if (rv < 0) return -1;
            pos += (size_t)rv;
        }
        fprintf(f, "(arr) end\n");
        return (long)pos;
    }
    default:
        return -1;
    }
}
