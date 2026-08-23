#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parser.h"

typedef struct {
    const char* s;
    size_t pos;
    size_t len;
    char errbuf[256];
    int has_error;
} Parser;

static void skip_ws(Parser* p) {
    for (;;) {
        while (p->pos < p->len && isspace((unsigned char)p->s[p->pos])) p->pos++;
        if (p->pos < p->len && p->s[p->pos] == ';') {
            while (p->pos < p->len && p->s[p->pos] != '\n') p->pos++;
            continue;
        }
        break;
    }
}

static int is_symbol_char(char c) {
    return isalnum((unsigned char)c) || strchr("_+-*/\\=<>!&|%^", c) != NULL;
}

static int looks_like_number(const char* tok, size_t len) {
    size_t i = 0;
    if (i < len && tok[i] == '-') i++;
    size_t d0 = i;
    while (i < len && isdigit((unsigned char)tok[i])) i++;
    if (i == d0) return 0;
    if (i < len && tok[i] == '.') {
        i++;
        size_t d1 = i;
        while (i < len && isdigit((unsigned char)tok[i])) i++;
        if (i == d1) return 0;
    }
    return i == len;
}

static void set_error(Parser* p, const char* msg) {
    if (!p->has_error) {
        snprintf(p->errbuf, sizeof(p->errbuf), "%s", msg);
        p->has_error = 1;
    }
}

static Lval* parse_expr(Parser* p);

static Lval* parse_string(Parser* p) {
    p->pos++;
    char buf[4096];
    size_t bi = 0;
    while (p->pos < p->len && p->s[p->pos] != '"') {
        char c = p->s[p->pos];
        if (c == '\\') {
            p->pos++;
            if (p->pos >= p->len) {
                set_error(p, "unterminated string escape");
                return NULL;
            }
            char e = p->s[p->pos];
            switch (e) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case '\\': c = '\\'; break;
                case '"': c = '"'; break;
                default: c = e; break;
            }
        }
        if (bi + 1 < sizeof(buf)) buf[bi++] = c;
        p->pos++;
    }
    if (p->pos >= p->len) {
        set_error(p, "unterminated string literal");
        return NULL;
    }
    p->pos++;
    buf[bi] = '\0';
    return lval_str(buf);
}

static Lval* parse_atom(Parser* p) {
    size_t start = p->pos;
    while (p->pos < p->len && is_symbol_char(p->s[p->pos])) p->pos++;
    size_t len = p->pos - start;
    if (len == 0) {
        set_error(p, "unexpected character");
        return NULL;
    }
    char tok[256];
    if (len >= sizeof(tok)) len = sizeof(tok) - 1;
    memcpy(tok, p->s + start, len);
    tok[len] = '\0';
    if (looks_like_number(tok, len)) {
        return lval_num(strtod(tok, NULL));
    }
    return lval_sym(tok);
}

static Lval* parse_list(Parser* p, char close, int is_qexpr) {
    p->pos++;
    Lval* v = is_qexpr ? lval_qexpr() : lval_sexpr();
    for (;;) {
        skip_ws(p);
        if (p->pos >= p->len) {
            set_error(p, is_qexpr ? "unmatched '{'" : "unmatched '('");
            lval_del(v);
            return NULL;
        }
        if (p->s[p->pos] == close) {
            p->pos++;
            break;
        }
        Lval* x = parse_expr(p);
        if (!x) {
            lval_del(v);
            return NULL;
        }
        lval_add(v, x);
    }
    return v;
}

static Lval* parse_expr(Parser* p) {
    skip_ws(p);
    if (p->pos >= p->len) {
        set_error(p, "unexpected end of input");
        return NULL;
    }
    char c = p->s[p->pos];
    if (c == '(') return parse_list(p, ')', 0);
    if (c == '{') return parse_list(p, '}', 1);
    if (c == ')' || c == '}') {
        set_error(p, "unexpected closing bracket");
        return NULL;
    }
    if (c == '"') return parse_string(p);
    return parse_atom(p);
}

Lval* parser_read(const char* input) {
    Parser p;
    p.s = input;
    p.pos = 0;
    p.len = strlen(input);
    p.has_error = 0;
    p.errbuf[0] = '\0';

    Lval* prog = lval_sexpr();
    for (;;) {
        skip_ws(&p);
        if (p.pos >= p.len) break;
        Lval* x = parse_expr(&p);
        if (!x) {
            lval_del(prog);
            Lval* wrap = lval_sexpr();
            lval_add(wrap, lval_err("Parse error: %s", p.errbuf));
            return wrap;
        }
        lval_add(prog, x);
    }
    return prog;
}
