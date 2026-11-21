/* lexer.h - turns PL/0 source text into tokens, one at a time. */
#ifndef PL0_LEXER_H
#define PL0_LEXER_H

#include <stddef.h>

#define PL0_MAX_IDENT 63

typedef enum {
    T_EOF,
    T_IDENT,
    T_NUMBER,
    T_STRING,
    /* keywords */
    T_CONST, T_VAR, T_SIZE, T_PROCEDURE, T_FORWARD, T_CALL,
    T_BEGIN, T_END, T_IF, T_THEN, T_ELSE, T_WHILE, T_DO,
    T_ODD, T_AND, T_OR, T_NOT,
    T_READINT, T_READCHAR, T_INTO, T_WRITEINT, T_WRITECHAR, T_WRITESTR, T_EXIT,
    /* punctuation and operators */
    T_DOT, T_COMMA, T_SEMICOLON, T_ASSIGN,
    T_EQUAL, T_HASH, T_LT, T_LE, T_GT, T_GE,
    T_PLUS, T_MINUS, T_STAR, T_SLASH, T_PERCENT,
    T_LPAREN, T_RPAREN, T_LBRACK, T_RBRACK,
    T_COUNT
} TokenKind;

typedef struct {
    TokenKind kind;
    int line;
    char name[PL0_MAX_IDENT + 1]; /* T_IDENT: the identifier */
    long long value;              /* T_NUMBER: the value */
    const char *str;              /* T_STRING: decoded bytes, owned by the lexer, valid until the next token */
    size_t str_len;
} Token;

typedef struct {
    const char *src;
    size_t len;
    size_t pos;
    int line;
    int last_line;    /* line of the previous token; end of file is reported there */
    char *strbuf;     /* growable buffer for decoded string literals */
    size_t strcap;
    char err[256];    /* set when lexer_next returns -1 */
} Lexer;

void lexer_init(Lexer *lx, const char *src, size_t len);
void lexer_free(Lexer *lx);

/* Reads the next token into *tok. Returns 0 on success, -1 on a lexical error
 * (message in lx->err, line in tok->line). After T_EOF it keeps returning T_EOF. */
int lexer_next(Lexer *lx, Token *tok);

/* Human-readable token name, e.g. "':='" or "'begin'" or "identifier". */
const char *token_name(TokenKind kind);

#endif
