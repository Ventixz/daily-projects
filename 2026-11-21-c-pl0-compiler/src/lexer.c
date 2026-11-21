/* lexer.c - hand-written PL/0 scanner.
 *
 * Comments are { ... } and may span lines. Strings are single-quoted; a doubled
 * quote ('') inside a string is one literal quote. Keywords are case-sensitive.
 */
#include "lexer.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct { const char *word; TokenKind kind; } keywords[] = {
    {"const", T_CONST},       {"var", T_VAR},           {"size", T_SIZE},
    {"procedure", T_PROCEDURE}, {"forward", T_FORWARD}, {"call", T_CALL},
    {"begin", T_BEGIN},       {"end", T_END},           {"if", T_IF},
    {"then", T_THEN},         {"else", T_ELSE},         {"while", T_WHILE},
    {"do", T_DO},             {"odd", T_ODD},           {"and", T_AND},
    {"or", T_OR},             {"not", T_NOT},           {"readInt", T_READINT},
    {"readChar", T_READCHAR}, {"into", T_INTO},         {"writeInt", T_WRITEINT},
    {"writeChar", T_WRITECHAR}, {"writeStr", T_WRITESTR}, {"exit", T_EXIT},
};

static const char *names[T_COUNT] = {
    [T_EOF] = "end of file", [T_IDENT] = "identifier", [T_NUMBER] = "number",
    [T_STRING] = "string",
    [T_CONST] = "'const'", [T_VAR] = "'var'", [T_SIZE] = "'size'",
    [T_PROCEDURE] = "'procedure'", [T_FORWARD] = "'forward'", [T_CALL] = "'call'",
    [T_BEGIN] = "'begin'", [T_END] = "'end'", [T_IF] = "'if'", [T_THEN] = "'then'",
    [T_ELSE] = "'else'", [T_WHILE] = "'while'", [T_DO] = "'do'", [T_ODD] = "'odd'",
    [T_AND] = "'and'", [T_OR] = "'or'", [T_NOT] = "'not'",
    [T_READINT] = "'readInt'", [T_READCHAR] = "'readChar'", [T_INTO] = "'into'",
    [T_WRITEINT] = "'writeInt'", [T_WRITECHAR] = "'writeChar'",
    [T_WRITESTR] = "'writeStr'", [T_EXIT] = "'exit'",
    [T_DOT] = "'.'", [T_COMMA] = "','", [T_SEMICOLON] = "';'", [T_ASSIGN] = "':='",
    [T_EQUAL] = "'='", [T_HASH] = "'#'", [T_LT] = "'<'", [T_LE] = "'<='",
    [T_GT] = "'>'", [T_GE] = "'>='", [T_PLUS] = "'+'", [T_MINUS] = "'-'",
    [T_STAR] = "'*'", [T_SLASH] = "'/'", [T_PERCENT] = "'%'",
    [T_LPAREN] = "'('", [T_RPAREN] = "')'", [T_LBRACK] = "'['", [T_RBRACK] = "']'",
};

const char *token_name(TokenKind kind) {
    if (kind < 0 || kind >= T_COUNT || !names[kind]) return "?";
    return names[kind];
}

void lexer_init(Lexer *lx, const char *src, size_t len) {
    memset(lx, 0, sizeof *lx);
    lx->src = src;
    lx->len = len;
    lx->line = 1;
    lx->last_line = 1;
}

void lexer_free(Lexer *lx) {
    free(lx->strbuf);
    lx->strbuf = NULL;
    lx->strcap = 0;
}

static int fail(Lexer *lx, const char *msg) {
    snprintf(lx->err, sizeof lx->err, "%s", msg);
    return -1;
}

static int peek(const Lexer *lx, size_t ahead) {
    size_t p = lx->pos + ahead;
    return p < lx->len ? (unsigned char)lx->src[p] : -1;
}

static int is_alpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static int is_digit(int c) { return c >= '0' && c <= '9'; }

/* Skips whitespace and comments. Returns -1 on an unterminated comment. */
static int skip_space(Lexer *lx, Token *tok) {
    for (;;) {
        int c = peek(lx, 0);
        if (c == '\n') {
            lx->line++;
            lx->pos++;
        } else if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') {
            lx->pos++;
        } else if (c == '{') {
            int start = lx->line;
            lx->pos++;
            for (;;) {
                c = peek(lx, 0);
                if (c == -1) {
                    tok->line = start;
                    return fail(lx, "unterminated comment");
                }
                lx->pos++;
                if (c == '\n') lx->line++;
                if (c == '}') break;
            }
        } else {
            return 0;
        }
    }
}

static int push_str(Lexer *lx, size_t *n, char c) {
    if (*n + 1 >= lx->strcap) {
        size_t cap = lx->strcap ? lx->strcap * 2 : 64;
        char *p = realloc(lx->strbuf, cap);
        if (!p) return fail(lx, "out of memory");
        lx->strbuf = p;
        lx->strcap = cap;
    }
    lx->strbuf[(*n)++] = c;
    lx->strbuf[*n] = '\0';
    return 0;
}

static int scan(Lexer *lx, Token *tok);

int lexer_next(Lexer *lx, Token *tok) {
    int rc = scan(lx, tok);
    if (rc == 0) {
        if (tok->kind == T_EOF) tok->line = lx->last_line; /* not the blank line after the code */
        else lx->last_line = tok->line;
    }
    return rc;
}

static int scan(Lexer *lx, Token *tok) {
    memset(tok, 0, sizeof *tok);
    if (skip_space(lx, tok) < 0) return -1;
    tok->line = lx->line;

    int c = peek(lx, 0);
    if (c == -1) {
        tok->kind = T_EOF;
        return 0;
    }
    if (c == '\0') return fail(lx, "NUL byte in source");

    if (is_alpha(c)) {
        size_t start = lx->pos;
        while (is_alpha(peek(lx, 0)) || is_digit(peek(lx, 0))) lx->pos++;
        size_t n = lx->pos - start;
        if (n > PL0_MAX_IDENT) return fail(lx, "identifier too long (max 63 characters)");
        memcpy(tok->name, lx->src + start, n);
        tok->name[n] = '\0';
        tok->kind = T_IDENT;
        for (size_t i = 0; i < sizeof keywords / sizeof keywords[0]; i++) {
            if (strcmp(tok->name, keywords[i].word) == 0) {
                tok->kind = keywords[i].kind;
                break;
            }
        }
        return 0;
    }

    if (is_digit(c)) {
        long long v = 0;
        while (is_digit(peek(lx, 0))) {
            int d = peek(lx, 0) - '0';
            if (v > (LLONG_MAX - d) / 10) return fail(lx, "number too large");
            v = v * 10 + d;
            lx->pos++;
        }
        if (is_alpha(peek(lx, 0))) return fail(lx, "malformed number");
        tok->kind = T_NUMBER;
        tok->value = v;
        return 0;
    }

    if (c == '\'') {
        size_t n = 0;
        int start = lx->line;
        lx->pos++;
        if (push_str(lx, &n, '\0') < 0) return -1; /* make sure strbuf exists */
        n = 0;
        for (;;) {
            int ch = peek(lx, 0);
            if (ch == -1 || ch == '\n') {
                tok->line = start;
                return fail(lx, "unterminated string");
            }
            lx->pos++;
            if (ch == '\'') {
                if (peek(lx, 0) != '\'') break;
                lx->pos++; /* '' is an escaped quote */
            }
            if (ch == '\0') return fail(lx, "NUL byte in string");
            if (push_str(lx, &n, (char)ch) < 0) return -1;
        }
        tok->kind = T_STRING;
        tok->str = lx->strbuf;
        tok->str_len = n;
        return 0;
    }

    lx->pos++;
    switch (c) {
    case '.': tok->kind = T_DOT; return 0;
    case ',': tok->kind = T_COMMA; return 0;
    case ';': tok->kind = T_SEMICOLON; return 0;
    case '=': tok->kind = T_EQUAL; return 0;
    case '#': tok->kind = T_HASH; return 0;
    case '+': tok->kind = T_PLUS; return 0;
    case '-': tok->kind = T_MINUS; return 0;
    case '*': tok->kind = T_STAR; return 0;
    case '/': tok->kind = T_SLASH; return 0;
    case '%': tok->kind = T_PERCENT; return 0;
    case '(': tok->kind = T_LPAREN; return 0;
    case ')': tok->kind = T_RPAREN; return 0;
    case '[': tok->kind = T_LBRACK; return 0;
    case ']': tok->kind = T_RBRACK; return 0;
    case ':':
        if (peek(lx, 0) == '=') {
            lx->pos++;
            tok->kind = T_ASSIGN;
            return 0;
        }
        return fail(lx, "expected '=' after ':'");
    case '<':
        if (peek(lx, 0) == '=') { lx->pos++; tok->kind = T_LE; } else tok->kind = T_LT;
        return 0;
    case '>':
        if (peek(lx, 0) == '=') { lx->pos++; tok->kind = T_GE; } else tok->kind = T_GT;
        return 0;
    }

    char msg[64];
    if (c >= 0x20 && c < 0x7f)
        snprintf(msg, sizeof msg, "unexpected character '%c'", c);
    else
        snprintf(msg, sizeof msg, "unexpected byte 0x%02x", c);
    return fail(lx, msg);
}
