/* Wire protocol.
 *
 * Every message on the socket is   [u32 body length][body]   (little-endian).
 *
 * Request body:   [u32 nstr] then nstr x ([u32 len][len bytes])
 * Response body:  one tagged value:
 *   NIL                       [tag]
 *   ERR  [u32 code][u32 len][msg]
 *   STR  [u32 len][bytes]
 *   INT  [i64]
 *   DBL  [f64]
 *   ARR  [u32 n] followed by n values
 * Length prefixes everywhere mean neither side ever scans for delimiters, and
 * values are binary-safe. */
#ifndef PROTO_H
#define PROTO_H

#include "buf.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

enum { TAG_NIL = 0, TAG_ERR = 1, TAG_STR = 2, TAG_INT = 3, TAG_DBL = 4, TAG_ARR = 5 };
enum { ERR_UNKNOWN = 1, ERR_TOO_BIG = 2, ERR_BAD_TYP = 3, ERR_BAD_ARG = 4 };

typedef struct {
    const char *ptr;  /* points into the request buffer; not NUL-terminated */
    size_t len;
} Str;

typedef struct {
    Str *v;
    size_t n;
} Strs;

/* Parse a request body. On success fills *out (caller frees out->v) and
 * returns 0; returns -1 for any malformed or trailing bytes. */
int parse_request(const uint8_t *data, size_t len, Strs *out);
/* Append a complete framed request to b. */
void encode_request(Buf *b, const Str *args, size_t n);

void out_nil(Buf *b);
void out_str(Buf *b, const char *s, size_t len);
void out_int(Buf *b, int64_t v);
void out_dbl(Buf *b, double v);
void out_err(Buf *b, uint32_t code, const char *msg);
/* Arrays whose length is only known at the end: begin returns a token that
 * end uses to patch the count in place. */
size_t out_begin_arr(Buf *b);
void out_end_arr(Buf *b, size_t token, uint32_t n);

/* Print one response value in a human-readable form. Returns bytes consumed,
 * or -1 if the data is malformed or truncated. */
long print_value(FILE *f, const uint8_t *data, size_t len);

#endif
