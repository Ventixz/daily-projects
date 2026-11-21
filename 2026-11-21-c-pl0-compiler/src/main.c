/* main.c - pl0c command-line driver.
 *
 *   pl0c [-o out.c] file.pl0      compile to C (stdout by default)
 *   pl0c -t file.pl0              print the token stream instead
 * Use "-" as the file name to read from stdin.
 */
#include "compiler.h"
#include "lexer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(void) {
    fprintf(stderr, "usage: pl0c [-t] [-o out.c] file.pl0\n");
    exit(2);
}

static char *read_all(FILE *fp, size_t *len) {
    size_t cap = 4096, n = 0;
    char *buf = malloc(cap);
    if (!buf) return NULL;
    for (;;) {
        if (n == cap) {
            char *p = realloc(buf, cap *= 2);
            if (!p) {
                free(buf);
                return NULL;
            }
            buf = p;
        }
        size_t got = fread(buf + n, 1, cap - n, fp);
        n += got;
        if (got == 0) break;
    }
    if (ferror(fp)) {
        free(buf);
        return NULL;
    }
    *len = n;
    return buf;
}

static int dump_tokens(const char *src, size_t len, const char *path) {
    Lexer lx;
    Token t;
    lexer_init(&lx, src, len);
    int rc = 0;
    for (;;) {
        if (lexer_next(&lx, &t) < 0) {
            fprintf(stderr, "pl0c: %s: line %d: %s\n", path, t.line, lx.err);
            rc = 1;
            break;
        }
        printf("%4d  %-12s", t.line, token_name(t.kind));
        if (t.kind == T_IDENT) printf(" %s", t.name);
        else if (t.kind == T_NUMBER) printf(" %lld", t.value);
        else if (t.kind == T_STRING) printf(" '%.*s'", (int)t.str_len, t.str);
        printf("\n");
        if (t.kind == T_EOF) break;
    }
    lexer_free(&lx);
    return rc;
}

int main(int argc, char **argv) {
    const char *in_path = NULL, *out_path = NULL;
    int tokens = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-t") == 0) tokens = 1;
        else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) out_path = argv[++i];
        else if (argv[i][0] == '-' && argv[i][1] != '\0') usage();
        else if (!in_path) in_path = argv[i];
        else usage();
    }
    if (!in_path) usage();

    FILE *in = strcmp(in_path, "-") == 0 ? stdin : fopen(in_path, "rb");
    if (!in) {
        perror(in_path);
        return 1;
    }
    size_t len = 0;
    char *src = read_all(in, &len);
    if (in != stdin) fclose(in);
    if (!src) {
        fprintf(stderr, "pl0c: cannot read %s\n", in_path);
        return 1;
    }

    if (tokens) {
        int rc = dump_tokens(src, len, in_path);
        free(src);
        return rc;
    }

    Buf out = {0};
    char err[256];
    if (pl0_compile(src, len, &out, err, sizeof err) != 0) {
        fprintf(stderr, "pl0c: %s: error: %s\n", in_path, err);
        free(src);
        buf_free(&out);
        return 1;
    }
    free(src);

    FILE *fp = out_path ? fopen(out_path, "w") : stdout;
    if (!fp) {
        perror(out_path);
        buf_free(&out);
        return 1;
    }
    int ok = fwrite(out.data, 1, out.len, fp) == out.len;
    if (out_path) ok = (fclose(fp) == 0) && ok;
    else ok = (fflush(fp) == 0) && ok;
    buf_free(&out);
    if (!ok) {
        fprintf(stderr, "pl0c: write failed\n");
        return 1;
    }
    return 0;
}
