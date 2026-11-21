/* unit_tests.c - in-process tests for the lexer and for compiler details that
 * are easier to check on the generated C than by running a program. */
#include "compiler.h"
#include "lexer.h"

#include <stdio.h>
#include <string.h>

static int failures, checks;

#define CHECK(cond, ...)                                                   \
    do {                                                                   \
        checks++;                                                          \
        if (!(cond)) {                                                     \
            failures++;                                                    \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                    \
            printf(__VA_ARGS__);                                           \
            printf("\n");                                                  \
        }                                                                  \
    } while (0)

/* Lexes src fully; returns the number of tokens (excluding EOF) or -1. */
static int lex_all(const char *src, Token *toks, int max, char *err) {
    Lexer lx;
    lexer_init(&lx, src, strlen(src));
    int n = 0;
    for (;;) {
        Token t;
        if (lexer_next(&lx, &t) < 0) {
            snprintf(err, 256, "line %d: %.200s", t.line, lx.err);
            lexer_free(&lx);
            return -1;
        }
        if (t.kind == T_EOF) break;
        if (n < max) {
            toks[n] = t;
            if (t.kind == T_STRING) toks[n].str = NULL; /* not valid after next call */
        }
        n++;
    }
    lexer_free(&lx);
    return n;
}

static void test_operators(void) {
    Token t[32];
    char err[256];
    int n = lex_all(":= = # < <= > >= + - * / % ( ) [ ] . , ;", t, 32, err);
    TokenKind want[] = {T_ASSIGN, T_EQUAL, T_HASH, T_LT, T_LE, T_GT, T_GE, T_PLUS,
                        T_MINUS, T_STAR, T_SLASH, T_PERCENT, T_LPAREN, T_RPAREN,
                        T_LBRACK, T_RBRACK, T_DOT, T_COMMA, T_SEMICOLON};
    int nwant = (int)(sizeof want / sizeof want[0]);
    CHECK(n == nwant, "operator count %d, want %d", n, nwant);
    for (int i = 0; i < n && i < nwant; i++)
        CHECK(t[i].kind == want[i], "token %d is %s, want %s", i, token_name(t[i].kind),
              token_name(want[i]));
}

static void test_keywords_and_idents(void) {
    Token t[16];
    char err[256];
    int n = lex_all("begin Begin readInt readint while_ x1 _y", t, 16, err);
    CHECK(n == 7, "count %d", n);
    CHECK(t[0].kind == T_BEGIN, "begin is a keyword");
    CHECK(t[1].kind == T_IDENT && strcmp(t[1].name, "Begin") == 0, "keywords are case-sensitive");
    CHECK(t[2].kind == T_READINT, "readInt is a keyword");
    CHECK(t[3].kind == T_IDENT, "readint is an identifier");
    CHECK(t[4].kind == T_IDENT && strcmp(t[4].name, "while_") == 0, "keyword prefix is an identifier");
    CHECK(t[5].kind == T_IDENT && strcmp(t[5].name, "x1") == 0, "digits in identifiers");
    CHECK(t[6].kind == T_IDENT && strcmp(t[6].name, "_y") == 0, "leading underscore");
}

static void test_numbers(void) {
    Token t[4];
    char err[256];
    int n = lex_all("0 42 9223372036854775807", t, 4, err);
    CHECK(n == 3, "count %d", n);
    CHECK(t[0].value == 0 && t[1].value == 42, "small numbers");
    CHECK(t[2].value == 9223372036854775807LL, "LLONG_MAX");
    CHECK(lex_all("9223372036854775808", t, 4, err) < 0 && strstr(err, "number too large"),
          "overflowing literal: %s", err);
    CHECK(lex_all("12abc", t, 4, err) < 0 && strstr(err, "malformed number"), "12abc: %s", err);
}

static void test_comments_and_lines(void) {
    Token t[8];
    char err[256];
    int n = lex_all("a { comment\nspanning\nlines } b\n\nc", t, 8, err);
    CHECK(n == 3, "count %d", n);
    CHECK(t[0].line == 1 && t[1].line == 3 && t[2].line == 5, "lines %d %d %d", t[0].line,
          t[1].line, t[2].line);
    CHECK(lex_all("x\n{ open", t, 8, err) < 0 && strcmp(err, "line 2: unterminated comment") == 0,
          "unterminated comment reports where it started: %s", err);
}

static void test_strings(void) {
    Lexer lx;
    Token t;
    const char *src = "'it''s' ''";
    lexer_init(&lx, src, strlen(src));
    CHECK(lexer_next(&lx, &t) == 0 && t.kind == T_STRING, "string token");
    CHECK(t.str_len == 4 && memcmp(t.str, "it's", 4) == 0, "doubled quote decodes to one");
    CHECK(lexer_next(&lx, &t) == 0 && t.kind == T_STRING && t.str_len == 0, "empty string");
    lexer_free(&lx);
    Token toks[4];
    char err[256];
    CHECK(lex_all("'abc\ndef'", toks, 4, err) < 0 && strstr(err, "unterminated string"),
          "strings stop at newline: %s", err);
}

static void test_bad_chars(void) {
    Token t[4];
    char err[256];
    CHECK(lex_all("a ! b", t, 4, err) < 0 && strcmp(err, "line 1: unexpected character '!'") == 0,
          "%s", err);
    CHECK(lex_all("a : b", t, 4, err) < 0 && strstr(err, "expected '=' after ':'"), "%s", err);
}

/* Compiles src; returns the generated C (static buffer) or NULL with err set. */
static char *compile(const char *src, char *err) {
    static Buf out;
    buf_free(&out);
    if (pl0_compile(src, strlen(src), &out, err, 256) != 0) return NULL;
    return out.data;
}

static void test_codegen_static_links(void) {
    char err[256];
    const char *src =
        "var g;\n"
        "procedure a;\n"
        "  var x;\n"
        "  procedure b;\n"
        "    procedure c; begin g := x; call a end;\n"
        "  call c;\n"
        "call b;\n"
        "call a.\n";
    char *c = compile(src, err);
    CHECK(c != NULL, "compiles: %s", err);
    if (!c) return;
    /* c is three levels deep: x is two hops out, g three. */
    CHECK(strstr(c, "f->up->up->up->v_g = f->up->up->v_x;") != NULL, "variable paths");
    /* a is declared in main (depth 0); calling it from depth 3 passes main's frame. */
    CHECK(strstr(c, "pl0_proc_1_a(f->up->up->up);") != NULL, "static link for call");
    CHECK(strstr(c, "pl0_proc_3_c(f);") != NULL, "call to a child passes our own frame");
    CHECK(strstr(c, "struct pl0_frame_3 {\n    struct pl0_frame_2 *up;\n};") != NULL,
          "frame chain types");
}

static void test_codegen_escapes(void) {
    char err[256];
    char *c = compile("begin writeStr 'a\"b\\c?\?=' end.", err); /* "?\?" avoids a trigraph here too */
    CHECK(c != NULL, "compiles: %s", err);
    if (c) CHECK(strstr(c, "fputs(\"a\\\"b\\\\c\\?\\?=\", stdout);") != NULL, "C escaping");
}

static void test_codegen_constants_inline(void) {
    char err[256];
    char *c = compile("const k = -3; var x; begin x := k * 2 end.", err);
    CHECK(c != NULL, "compiles: %s", err);
    if (c) CHECK(strstr(c, "f->v_x = pl0_mul((-3LL), 2LL, 1);") != NULL, "constants are inlined");
}

static void test_error_stops_at_first(void) {
    char err[256];
    CHECK(compile("var x;\nbegin\n  x := 1;\n  y := 2;\n  z := 3\nend.", err) == NULL, "fails");
    CHECK(strcmp(err, "line 4: undeclared identifier 'y'") == 0, "first error wins: %s", err);
}

static void test_forward_then_define(void) {
    char err[256];
    CHECK(compile("forward p; procedure p; ; procedure p; ; .", err) == NULL, "redefinition");
    CHECK(strstr(err, "'p' is already declared") != NULL, "%s", err);
    CHECK(compile("forward p; forward p; .", err) == NULL, "double forward");
    CHECK(compile("procedure p; forward q; ; .", err) == NULL, "inner forward undefined");
    CHECK(strstr(err, "forward procedure 'q' is never defined") != NULL, "%s", err);
}

int main(void) {
    test_operators();
    test_keywords_and_idents();
    test_numbers();
    test_comments_and_lines();
    test_strings();
    test_bad_chars();
    test_codegen_static_links();
    test_codegen_escapes();
    test_codegen_constants_inline();
    test_error_stops_at_first();
    test_forward_then_define();
    printf("unit: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
