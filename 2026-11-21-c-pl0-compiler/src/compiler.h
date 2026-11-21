/* compiler.h - single-pass PL/0 -> C compiler. */
#ifndef PL0_COMPILER_H
#define PL0_COMPILER_H

#include <stddef.h>

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} Buf;

void buf_free(Buf *b);

/* Compiles PL/0 source to a complete C11 translation unit appended to *out.
 * Returns 0 on success. On error returns -1 and writes "line N: message" to err. */
int pl0_compile(const char *src, size_t len, Buf *out, char *err, size_t errlen);

#endif
