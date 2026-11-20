#include "buf.h"
#include "common.h"

#include <stdio.h>
#include <stdlib.h>

static void buf_reserve(Buf *b, size_t extra) {
    if (b->off + b->len + extra <= b->cap) return;
    /* Reuse space already consumed at the front before growing. Moving is
     * O(len), but it only happens once the dead prefix outweighs live data. */
    if (b->off > 0 && b->len + extra <= b->cap && b->off >= b->len) {
        memmove(b->mem, b->mem + b->off, b->len);
        b->off = 0;
        return;
    }
    size_t need = b->len + extra;
    size_t cap = b->cap ? b->cap : 64;
    while (cap < need) cap *= 2;
    uint8_t *mem = malloc(cap);
    if (!mem) { perror("malloc"); abort(); }
    if (b->len) memcpy(mem, b->mem + b->off, b->len);
    free(b->mem);
    b->mem = mem;
    b->off = 0;
    b->cap = cap;
}

void buf_append(Buf *b, const void *data, size_t n) {
    if (n == 0) return;
    buf_reserve(b, n);
    memcpy(b->mem + b->off + b->len, data, n);
    b->len += n;
}

void buf_append_u8(Buf *b, uint8_t v) { buf_append(b, &v, 1); }

void buf_append_u32(Buf *b, uint32_t v) {
    uint8_t tmp[4];
    wr_u32(tmp, v);
    buf_append(b, tmp, 4);
}

void buf_append_u64(Buf *b, uint64_t v) {
    uint8_t tmp[8];
    wr_u64(tmp, v);
    buf_append(b, tmp, 8);
}

void buf_consume(Buf *b, size_t n) {
    if (n >= b->len) { b->off = 0; b->len = 0; return; }
    b->off += n;
    b->len -= n;
}

void buf_truncate(Buf *b, size_t new_len) {
    if (new_len < b->len) b->len = new_len;
}

void buf_free(Buf *b) {
    free(b->mem);
    b->mem = NULL;
    b->off = b->len = b->cap = 0;
}
