/* A growable byte buffer that is cheap to consume from the front. */
#ifndef BUF_H
#define BUF_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t *mem;   /* allocation */
    size_t off;     /* first live byte */
    size_t len;     /* live bytes, starting at mem + off */
    size_t cap;     /* allocation size */
} Buf;

static inline uint8_t *buf_data(Buf *b) { return b->mem + b->off; }
void buf_append(Buf *b, const void *data, size_t n);
void buf_append_u8(Buf *b, uint8_t v);
void buf_append_u32(Buf *b, uint32_t v);
void buf_append_u64(Buf *b, uint64_t v);
void buf_consume(Buf *b, size_t n);
void buf_truncate(Buf *b, size_t new_len);
void buf_free(Buf *b);

#endif
