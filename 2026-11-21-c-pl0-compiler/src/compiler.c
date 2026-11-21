/* compiler.c - single-pass PL/0 -> C compiler.
 *
 * A recursive-descent parser that type-checks and emits C as it goes; there is
 * no AST. Statements are written straight to the output. Expressions and
 * conditions are built up as small C strings first, because a left-associative
 * chain like a - b - c has to become pl0_sub(pl0_sub(a, b), c), and we only know
 * how deep the nesting is after we've read the whole chain.
 *
 * Nested procedures: PL/0 lets an inner procedure read and write the variables
 * of every procedure around it, and procedures can be recursive. C has neither
 * nested functions nor closures, so each block gets a heap-allocated *frame*
 * struct holding its variables plus an `up` pointer to the frame of the block it
 * is nested in (the "static link"). A variable declared k levels out is reached
 * as f->up->...->up->v_name (k hops). Every activation gets a fresh frame, so
 * recursion keeps each call's locals separate.
 *
 * Grammar (EBNF):
 *   program    = block "." .
 *   block      = [ "const" ident "=" ["-"] number { "," ident "=" ["-"] number } ";" ]
 *                [ "var" vardecl { "," vardecl } ";" ]
 *                { "forward" ident ";" | "procedure" ident ";" block ";" }
 *                statement .
 *   vardecl    = ident [ "size" ( number | ident ) ] .
 *   statement  = [ lvalue ":=" expression
 *                | "call" ident
 *                | "begin" statement { ";" statement } "end"
 *                | "if" condition "then" statement [ "else" statement ]
 *                | "while" condition "do" statement
 *                | "readInt" "into" lvalue | "readChar" "into" lvalue
 *                | "writeInt" expression | "writeChar" expression
 *                | "writeStr" ( string | ident )
 *                | "exit" expression ] .
 *   lvalue     = ident [ "[" expression "]" ] .
 *   condition  = conj { "or" conj } .
 *   conj       = cterm { "and" cterm } .
 *   cterm      = "not" cterm | "odd" expression
 *              | expression ( "=" | "#" | "<" | "<=" | ">" | ">=" ) expression .
 *   expression = [ "+" | "-" ] term { ( "+" | "-" ) term } .
 *   term       = factor { ( "*" | "/" | "%" ) factor } .
 *   factor     = ident [ "[" expression "]" ] | number | "(" expression ")" .
 */
#include "compiler.h"
#include "lexer.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ARRAY_SIZE (1LL << 20)
#define MAX_NESTING 64        /* procedure nesting */
#define MAX_RECURSION 1000    /* nested expressions/statements, so hostile input can't blow the stack */

/* ---------- output buffer ---------- */

void buf_free(Buf *b) {
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}

static int buf_vprintf(Buf *b, const char *fmt, va_list ap) {
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap2);
    va_end(ap2);
    if (n < 0) return -1;
    if (b->len + (size_t)n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 256;
        while (b->len + (size_t)n + 1 > cap) cap *= 2;
        char *p = realloc(b->data, cap);
        if (!p) return -1;
        b->data = p;
        b->cap = cap;
    }
    vsnprintf(b->data + b->len, (size_t)n + 1, fmt, ap);
    b->len += (size_t)n;
    return 0;
}

static int buf_printf(Buf *b, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int rc = buf_vprintf(b, fmt, ap);
    va_end(ap);
    return rc;
}

/* ---------- compiler state ---------- */

typedef enum { S_CONST, S_VAR, S_ARRAY, S_PROC } SymKind;

typedef struct {
    char name[PL0_MAX_IDENT + 1];
    SymKind kind;
    int depth;        /* nesting depth of the block that declared it (main = 0) */
    int line;
    long long value;  /* S_CONST */
    long long size;   /* S_ARRAY */
    int proc_id;      /* S_PROC: unique id, also the id of the procedure's frame struct */
    int defined;      /* S_PROC: 0 while only forward-declared */
} Symbol;

typedef struct {
    Lexer lx;
    Token tok;
    Buf *out;
    int indent;
    int depth;
    int next_id;
    int recursion;    /* current parser recursion depth */
    Symbol *syms;
    size_t nsyms, capsyms;
    char **strs;      /* every expression string we allocate, freed at the end */
    size_t nstrs, capstrs;
    jmp_buf fail;
    char *err;
    size_t errlen;
} Compiler;

static void error_at(Compiler *c, int line, const char *fmt, ...) {
    char msg[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    snprintf(c->err, c->errlen, "line %d: %s", line, msg);
    longjmp(c->fail, 1);
}

static void emit(Compiler *c, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int rc = buf_vprintf(c->out, fmt, ap);
    va_end(ap);
    if (rc < 0) error_at(c, c->tok.line, "out of memory");
}

/* One indented line of output. */
static void line(Compiler *c, const char *fmt, ...) {
    for (int i = 0; i < c->indent; i++) emit(c, "    ");
    va_list ap;
    va_start(ap, fmt);
    int rc = buf_vprintf(c->out, fmt, ap);
    va_end(ap);
    if (rc < 0) error_at(c, c->tok.line, "out of memory");
    emit(c, "\n");
}

/* printf into a fresh string owned by the compiler. */
static char *sfmt(Compiler *c, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    char *s = n >= 0 ? malloc((size_t)n + 1) : NULL;
    if (!s) error_at(c, c->tok.line, "out of memory");
    va_start(ap, fmt);
    vsnprintf(s, (size_t)n + 1, fmt, ap);
    va_end(ap);
    if (c->nstrs == c->capstrs) {
        size_t cap = c->capstrs ? c->capstrs * 2 : 64;
        char **p = realloc(c->strs, cap * sizeof *p);
        if (!p) {
            free(s);
            error_at(c, c->tok.line, "out of memory");
        }
        c->strs = p;
        c->capstrs = cap;
    }
    c->strs[c->nstrs++] = s;
    return s;
}

/* ---------- tokens ---------- */

static void next(Compiler *c) {
    if (lexer_next(&c->lx, &c->tok) < 0) error_at(c, c->tok.line, "%s", c->lx.err);
}

static int accept(Compiler *c, TokenKind k) {
    if (c->tok.kind != k) return 0;
    next(c);
    return 1;
}

static void expect(Compiler *c, TokenKind k) {
    if (c->tok.kind != k) {
        const char *got = c->tok.kind == T_IDENT ? sfmt(c, "identifier '%s'", c->tok.name)
                                                 : token_name(c->tok.kind);
        error_at(c, c->tok.line, "expected %s but found %s", token_name(k), got);
    }
    next(c);
}

/* Consumes an identifier, copying it into out. Returns its line. */
static int expect_ident(Compiler *c, char out[PL0_MAX_IDENT + 1]) {
    int ln = c->tok.line;
    if (c->tok.kind != T_IDENT)
        error_at(c, ln, "expected identifier but found %s", token_name(c->tok.kind));
    memcpy(out, c->tok.name, PL0_MAX_IDENT + 1);
    next(c);
    return ln;
}

/* ---------- symbol table ---------- */

static Symbol *lookup(Compiler *c, const char *name) {
    for (size_t i = c->nsyms; i-- > 0;)  /* innermost first: shadowing just works */
        if (strcmp(c->syms[i].name, name) == 0) return &c->syms[i];
    return NULL;
}

static Symbol *lookup_or_fail(Compiler *c, const char *name, int ln) {
    Symbol *s = lookup(c, name);
    if (!s) error_at(c, ln, "undeclared identifier '%s'", name);
    return s;
}

static Symbol *in_scope(Compiler *c, size_t scope_start, const char *name) {
    for (size_t i = scope_start; i < c->nsyms; i++)
        if (strcmp(c->syms[i].name, name) == 0) return &c->syms[i];
    return NULL;
}

static Symbol *declare(Compiler *c, size_t scope_start, const char *name, SymKind kind, int ln) {
    Symbol *dup = in_scope(c, scope_start, name);
    if (dup) error_at(c, ln, "'%s' is already declared on line %d", name, dup->line);
    if (c->nsyms == c->capsyms) {
        size_t cap = c->capsyms ? c->capsyms * 2 : 32;
        Symbol *p = realloc(c->syms, cap * sizeof *p);
        if (!p) error_at(c, ln, "out of memory");
        c->syms = p;
        c->capsyms = cap;
    }
    Symbol *s = &c->syms[c->nsyms++];
    memset(s, 0, sizeof *s);
    memcpy(s->name, name, PL0_MAX_IDENT + 1);
    s->kind = kind;
    s->depth = c->depth;
    s->line = ln;
    return s;
}

static const char *kind_name(SymKind k) {
    switch (k) {
    case S_CONST: return "constant";
    case S_VAR: return "variable";
    case S_ARRAY: return "array";
    case S_PROC: return "procedure";
    }
    return "?";
}

/* "f->up->up" : the frame `hops` levels out from the current one. */
static char *frame_path(Compiler *c, int hops) {
    char *p = sfmt(c, "f");
    for (int i = 0; i < hops; i++) p = sfmt(c, "%s->up", p);
    return p;
}

static char *var_path(Compiler *c, const Symbol *s) {
    return sfmt(c, "%s->v_%s", frame_path(c, c->depth - s->depth), s->name);
}

static char *literal(Compiler *c, long long v) {
    return v < 0 ? sfmt(c, "(%lldLL)", v) : sfmt(c, "%lldLL", v);
}

/* ---------- expressions ---------- */

static char *expression(Compiler *c);
static char *expression_inner(Compiler *c);

/* name [ "[" expression "]" ] for a variable or array, after the identifier was read. */
static char *indexed(Compiler *c, Symbol *s, int ln) {
    if (s->kind == S_ARRAY) {
        if (c->tok.kind != T_LBRACK) error_at(c, ln, "array '%s' needs an index", s->name);
        next(c);
        char *idx = expression(c);
        expect(c, T_RBRACK);
        return sfmt(c, "%s[pl0_idx(%s, %lldLL, %d)]", var_path(c, s), idx, s->size, ln);
    }
    if (c->tok.kind == T_LBRACK) error_at(c, ln, "'%s' is not an array", s->name);
    return var_path(c, s);
}

static char *factor(Compiler *c) {
    int ln = c->tok.line;
    if (c->tok.kind == T_IDENT) {
        char name[PL0_MAX_IDENT + 1];
        expect_ident(c, name);
        Symbol *s = lookup_or_fail(c, name, ln);
        switch (s->kind) {
        case S_CONST: return literal(c, s->value);
        case S_VAR:
        case S_ARRAY: return indexed(c, s, ln);
        case S_PROC: error_at(c, ln, "procedure '%s' cannot be used as a value", name);
        }
    }
    if (c->tok.kind == T_NUMBER) {
        long long v = c->tok.value;
        next(c);
        return literal(c, v);
    }
    if (accept(c, T_LPAREN)) {
        char *e = expression(c);
        expect(c, T_RPAREN);
        return sfmt(c, "(%s)", e);
    }
    error_at(c, ln, "expected an expression but found %s", token_name(c->tok.kind));
    return NULL;
}

static char *term(Compiler *c) {
    char *left = factor(c);
    for (;;) {
        int ln = c->tok.line;
        const char *fn;
        if (accept(c, T_STAR)) fn = "pl0_mul";
        else if (accept(c, T_SLASH)) fn = "pl0_div";
        else if (accept(c, T_PERCENT)) fn = "pl0_mod";
        else return left;
        char *right = factor(c);
        left = sfmt(c, "%s(%s, %s, %d)", fn, left, right, ln);
    }
}

static void enter(Compiler *c) {
    if (++c->recursion > MAX_RECURSION) error_at(c, c->tok.line, "nested too deeply");
}

static char *expression(Compiler *c) {
    enter(c);
    char *e = expression_inner(c);
    c->recursion--;
    return e;
}

static char *expression_inner(Compiler *c) {
    int ln = c->tok.line;
    int neg = 0;
    if (accept(c, T_MINUS)) neg = 1;
    else accept(c, T_PLUS);
    char *left = term(c);
    if (neg) left = sfmt(c, "pl0_neg(%s, %d)", left, ln);
    for (;;) {
        ln = c->tok.line;
        const char *fn;
        if (accept(c, T_PLUS)) fn = "pl0_add";
        else if (accept(c, T_MINUS)) fn = "pl0_sub";
        else return left;
        char *right = term(c);
        left = sfmt(c, "%s(%s, %s, %d)", fn, left, right, ln);
    }
}

/* ---------- conditions ---------- */

static char *cterm(Compiler *c) {
    /* Every condition comes back fully parenthesized, so `if %s` is valid C. */
    if (accept(c, T_NOT)) {
        enter(c); /* "not not not ..." recurses without going through expression() */
        char *inner = cterm(c);
        c->recursion--;
        return sfmt(c, "(!%s)", inner);
    }
    if (accept(c, T_ODD)) return sfmt(c, "(%s %% 2 != 0)", expression(c));
    char *left = expression(c);
    const char *op;
    switch (c->tok.kind) {
    case T_EQUAL: op = "=="; break;
    case T_HASH: op = "!="; break;
    case T_LT: op = "<"; break;
    case T_LE: op = "<="; break;
    case T_GT: op = ">"; break;
    case T_GE: op = ">="; break;
    default:
        error_at(c, c->tok.line, "expected a comparison operator but found %s",
                 token_name(c->tok.kind));
        return NULL;
    }
    next(c);
    char *right = expression(c);
    return sfmt(c, "(%s %s %s)", left, op, right);
}

static char *conjunction(Compiler *c) {
    char *left = cterm(c);
    while (accept(c, T_AND)) left = sfmt(c, "(%s && %s)", left, cterm(c));
    return left;
}

static char *condition(Compiler *c) {
    char *left = conjunction(c);
    while (accept(c, T_OR)) left = sfmt(c, "(%s || %s)", left, conjunction(c));
    return left;
}

/* ---------- statements ---------- */

static char *lvalue(Compiler *c) {
    char name[PL0_MAX_IDENT + 1];
    int ln = expect_ident(c, name);
    Symbol *s = lookup_or_fail(c, name, ln);
    if (s->kind == S_CONST || s->kind == S_PROC)
        error_at(c, ln, "cannot assign to %s '%s'", kind_name(s->kind), name);
    return indexed(c, s, ln);
}

/* A PL/0 string as a C string literal. '?' is escaped so "??=" can't become a trigraph. */
static char *c_string(Compiler *c, const char *s, size_t n) {
    Buf b = {0};
    int rc = 0;
    rc |= buf_printf(&b, "\"");
    for (size_t i = 0; i < n; i++) {
        unsigned char ch = (unsigned char)s[i];
        if (ch == '"' || ch == '\\' || ch == '?') rc |= buf_printf(&b, "\\%c", ch);
        else if (ch >= 0x20 && ch < 0x7f) rc |= buf_printf(&b, "%c", ch);
        else rc |= buf_printf(&b, "\\%03o", ch);
    }
    rc |= buf_printf(&b, "\"");
    if (rc) {
        buf_free(&b);
        error_at(c, c->tok.line, "out of memory");
    }
    char *out = sfmt(c, "%s", b.data);
    buf_free(&b);
    return out;
}

static void statement(Compiler *c);

/* A sub-statement inside braces, so every if/while body is a C block. */
static void body(Compiler *c) {
    c->indent++;
    statement(c);
    c->indent--;
}

static void statement_inner(Compiler *c);

static void statement(Compiler *c) {
    enter(c);
    statement_inner(c);
    c->recursion--;
}

static void statement_inner(Compiler *c) {
    int ln = c->tok.line;
    switch (c->tok.kind) {
    case T_IDENT: {
        char *target = lvalue(c);
        expect(c, T_ASSIGN);
        line(c, "%s = %s;", target, expression(c));
        return;
    }
    case T_CALL: {
        next(c);
        char name[PL0_MAX_IDENT + 1];
        expect_ident(c, name);
        Symbol *s = lookup_or_fail(c, name, ln);
        if (s->kind != S_PROC) error_at(c, ln, "cannot call %s '%s'", kind_name(s->kind), name);
        /* The callee's static link is the frame of the block that declared it. */
        line(c, "pl0_proc_%d_%s(%s);", s->proc_id, s->name, frame_path(c, c->depth - s->depth));
        return;
    }
    case T_BEGIN:
        next(c);
        line(c, "{");
        body(c);
        while (accept(c, T_SEMICOLON)) body(c);
        expect(c, T_END);
        line(c, "}");
        return;
    case T_IF:
        next(c);
        line(c, "if %s {", condition(c));
        expect(c, T_THEN);
        body(c);
        if (accept(c, T_ELSE)) {   /* dangling else binds to the nearest if */
            line(c, "} else {");
            body(c);
        }
        line(c, "}");
        return;
    case T_WHILE:
        next(c);
        line(c, "while %s {", condition(c));
        expect(c, T_DO);
        body(c);
        line(c, "}");
        return;
    case T_READINT:
    case T_READCHAR: {
        int is_int = c->tok.kind == T_READINT;
        next(c);
        expect(c, T_INTO);
        char *target = lvalue(c);
        if (is_int) line(c, "%s = pl0_read_int(%d);", target, ln);
        else line(c, "%s = pl0_read_char();", target);
        return;
    }
    case T_WRITEINT:
        next(c);
        line(c, "pl0_write_int(%s);", expression(c));
        return;
    case T_WRITECHAR:
        next(c);
        line(c, "pl0_write_char(%s, %d);", expression(c), ln);
        return;
    case T_WRITESTR:
        next(c);
        if (c->tok.kind == T_STRING) {
            line(c, "fputs(%s, stdout);", c_string(c, c->tok.str, c->tok.str_len));
            next(c);
            return;
        }
        if (c->tok.kind == T_IDENT) {
            char name[PL0_MAX_IDENT + 1];
            expect_ident(c, name);
            Symbol *s = lookup_or_fail(c, name, ln);
            if (s->kind != S_ARRAY)
                error_at(c, ln, "writeStr needs a string or an array, but '%s' is a %s", name,
                         kind_name(s->kind));
            line(c, "pl0_write_arr(%s, %lldLL, %d);", var_path(c, s), s->size, ln);
            return;
        }
        error_at(c, ln, "writeStr needs a string or an array");
        return;
    case T_EXIT:
        next(c);
        line(c, "pl0_exit(%s);", expression(c));
        return;
    default:
        return; /* the empty statement */
    }
}

/* ---------- declarations and blocks ---------- */

static long long const_value(Compiler *c) {
    int neg = accept(c, T_MINUS);
    if (c->tok.kind != T_NUMBER)
        error_at(c, c->tok.line, "expected a number but found %s", token_name(c->tok.kind));
    long long v = c->tok.value;
    next(c);
    return neg ? -v : v;
}

static long long array_size(Compiler *c) {
    int ln = c->tok.line;
    long long n;
    if (c->tok.kind == T_IDENT) {
        char name[PL0_MAX_IDENT + 1];
        expect_ident(c, name);
        Symbol *s = lookup_or_fail(c, name, ln);
        if (s->kind != S_CONST) error_at(c, ln, "array size must be a number or a constant");
        n = s->value;
    } else {
        n = const_value(c);
    }
    if (n < 1 || n > MAX_ARRAY_SIZE)
        error_at(c, ln, "array size must be between 1 and %lld", MAX_ARRAY_SIZE);
    return n;
}

static const char *frame_type(Compiler *c, int id) { return sfmt(c, "struct pl0_frame_%d", id); }

static void prototype(Compiler *c, const Symbol *proc, int parent_id) {
    line(c, "static PL0_UNUSED void pl0_proc_%d_%s(%s *up);", proc->proc_id, proc->name,
         frame_type(c, parent_id));
}

/* id: this block's frame id (main = 0). parent_id: enclosing block's id, or -1 for main. */
static void block(Compiler *c, int id, int parent_id, const char *proc_name) {
    size_t scope = c->nsyms;
    if (c->depth > MAX_NESTING) error_at(c, c->tok.line, "procedures nested too deeply");

    if (accept(c, T_CONST)) {
        do {
            char name[PL0_MAX_IDENT + 1];
            int ln = expect_ident(c, name);
            expect(c, T_EQUAL);
            long long v = const_value(c);
            declare(c, scope, name, S_CONST, ln)->value = v;
        } while (accept(c, T_COMMA));
        expect(c, T_SEMICOLON);
    }

    if (accept(c, T_VAR)) {
        do {
            char name[PL0_MAX_IDENT + 1];
            int ln = expect_ident(c, name);
            if (accept(c, T_SIZE)) {
                long long n = array_size(c);
                declare(c, scope, name, S_ARRAY, ln)->size = n;
            } else {
                declare(c, scope, name, S_VAR, ln);
            }
        } while (accept(c, T_COMMA));
        expect(c, T_SEMICOLON);
    }

    /* This block's frame: its variables plus the static link. */
    emit(c, "\n");
    line(c, "%s {", frame_type(c, id));
    if (parent_id < 0) line(c, "    void *up;");
    else line(c, "    %s *up;", frame_type(c, parent_id));
    for (size_t i = scope; i < c->nsyms; i++) {
        const Symbol *s = &c->syms[i];
        if (s->kind == S_VAR) line(c, "    long long v_%s;", s->name);
        else if (s->kind == S_ARRAY) line(c, "    long long v_%s[%lld];", s->name, s->size);
    }
    line(c, "};");

    for (;;) {
        if (accept(c, T_FORWARD)) {
            char name[PL0_MAX_IDENT + 1];
            int ln = expect_ident(c, name);
            Symbol *s = declare(c, scope, name, S_PROC, ln);
            s->proc_id = c->next_id++;
            prototype(c, s, id);
            expect(c, T_SEMICOLON);
        } else if (accept(c, T_PROCEDURE)) {
            char name[PL0_MAX_IDENT + 1];
            int ln = expect_ident(c, name);
            Symbol *s = in_scope(c, scope, name);
            if (s && s->kind == S_PROC && !s->defined) {
                s->line = ln; /* completing a forward declaration */
            } else {
                s = declare(c, scope, name, S_PROC, ln);
                s->proc_id = c->next_id++;
                emit(c, "\n");
                prototype(c, s, id);
            }
            s->defined = 1;
            int proc_id = s->proc_id; /* s may move if the table grows */
            expect(c, T_SEMICOLON);
            c->depth++;
            block(c, proc_id, id, name);
            c->depth--;
            expect(c, T_SEMICOLON);
        } else {
            break;
        }
    }

    for (size_t i = scope; i < c->nsyms; i++) {
        const Symbol *s = &c->syms[i];
        if (s->kind == S_PROC && !s->defined)
            error_at(c, s->line, "forward procedure '%s' is never defined", s->name);
    }

    emit(c, "\n");
    if (parent_id < 0) {
        line(c, "int main(void) {");
        line(c, "    %s *f = pl0_alloc(sizeof *f);", frame_type(c, id));
        line(c, "    f->up = NULL;");
    } else {
        line(c, "static void pl0_proc_%d_%s(%s *up) {", id, proc_name, frame_type(c, parent_id));
        line(c, "    %s *f = pl0_alloc(sizeof *f);", frame_type(c, id));
        line(c, "    f->up = up;");
    }
    c->indent++;
    statement(c);
    line(c, "free(f);");
    if (parent_id < 0) line(c, "return 0;");
    c->indent--;
    line(c, "}");

    c->nsyms = scope; /* leaving the block: its names go out of scope */
}

/* ---------- runtime support emitted ahead of the program ---------- */

static const char *runtime =
    "/* Generated by pl0c. Do not edit; edit the .pl0 source instead. */\n"
    "#include <limits.h>\n"
    "#include <stdio.h>\n"
    "#include <stdlib.h>\n"
    "\n"
    "#if defined(__GNUC__) || defined(__clang__)\n"
    "#define PL0_UNUSED __attribute__((unused))\n"
    "#else\n"
    "#define PL0_UNUSED\n"
    "#endif\n"
    "\n"
    "static void pl0_fail(int line, const char *msg) {\n"
    "    fflush(stdout);\n"
    "    fprintf(stderr, \"runtime error: line %d: %s\\n\", line, msg);\n"
    "    exit(1);\n"
    "}\n"
    "\n"
    "static PL0_UNUSED void *pl0_alloc(size_t n) {\n"
    "    void *p = calloc(1, n);\n"
    "    if (!p) {\n"
    "        fprintf(stderr, \"runtime error: out of memory\\n\");\n"
    "        exit(1);\n"
    "    }\n"
    "    return p;\n"
    "}\n"
    "\n"
    "/* Signed overflow is undefined behaviour in C, so every operation checks first. */\n"
    "static PL0_UNUSED long long pl0_add(long long a, long long b, int line) {\n"
    "    if ((b > 0 && a > LLONG_MAX - b) || (b < 0 && a < LLONG_MIN - b)) pl0_fail(line, \"integer overflow\");\n"
    "    return a + b;\n"
    "}\n"
    "\n"
    "static PL0_UNUSED long long pl0_sub(long long a, long long b, int line) {\n"
    "    if ((b < 0 && a > LLONG_MAX + b) || (b > 0 && a < LLONG_MIN + b)) pl0_fail(line, \"integer overflow\");\n"
    "    return a - b;\n"
    "}\n"
    "\n"
    "static PL0_UNUSED long long pl0_mul(long long a, long long b, int line) {\n"
    "    if (a > 0 ? (b > 0 ? a > LLONG_MAX / b : b < LLONG_MIN / a)\n"
    "              : (b > 0 ? a < LLONG_MIN / b : (a != 0 && b < LLONG_MAX / a)))\n"
    "        pl0_fail(line, \"integer overflow\");\n"
    "    return a * b;\n"
    "}\n"
    "\n"
    "static PL0_UNUSED long long pl0_div(long long a, long long b, int line) {\n"
    "    if (b == 0) pl0_fail(line, \"division by zero\");\n"
    "    if (a == LLONG_MIN && b == -1) pl0_fail(line, \"integer overflow\");\n"
    "    return a / b;\n"
    "}\n"
    "\n"
    "static PL0_UNUSED long long pl0_mod(long long a, long long b, int line) {\n"
    "    if (b == 0) pl0_fail(line, \"division by zero\");\n"
    "    if (b == -1) return 0;\n"
    "    return a % b;\n"
    "}\n"
    "\n"
    "static PL0_UNUSED long long pl0_neg(long long a, int line) {\n"
    "    if (a == LLONG_MIN) pl0_fail(line, \"integer overflow\");\n"
    "    return -a;\n"
    "}\n"
    "\n"
    "static PL0_UNUSED long long pl0_idx(long long i, long long n, int line) {\n"
    "    if (i < 0 || i >= n) {\n"
    "        char msg[96];\n"
    "        snprintf(msg, sizeof msg, \"array index %lld out of range 0..%lld\", i, n - 1);\n"
    "        pl0_fail(line, msg);\n"
    "    }\n"
    "    return i;\n"
    "}\n"
    "\n"
    "static PL0_UNUSED long long pl0_read_int(int line) {\n"
    "    int ch;\n"
    "    do ch = getchar(); while (ch == ' ' || ch == '\\t' || ch == '\\n' || ch == '\\r');\n"
    "    if (ch == EOF) pl0_fail(line, \"readInt: end of input\");\n"
    "    int neg = 0;\n"
    "    if (ch == '-' || ch == '+') {\n"
    "        neg = ch == '-';\n"
    "        ch = getchar();\n"
    "    }\n"
    "    if (ch < '0' || ch > '9') pl0_fail(line, \"readInt: expected an integer\");\n"
    "    long long v = 0; /* accumulate negatively so LLONG_MIN is readable */\n"
    "    while (ch >= '0' && ch <= '9') {\n"
    "        int d = ch - '0';\n"
    "        if (v < (LLONG_MIN + d) / 10) pl0_fail(line, \"readInt: number too large\");\n"
    "        v = v * 10 - d;\n"
    "        ch = getchar();\n"
    "    }\n"
    "    if (ch != EOF) ungetc(ch, stdin);\n"
    "    if (!neg && v == LLONG_MIN) pl0_fail(line, \"readInt: number too large\");\n"
    "    return neg ? v : -v;\n"
    "}\n"
    "\n"
    "static PL0_UNUSED long long pl0_read_char(void) {\n"
    "    int ch = getchar();\n"
    "    return ch == EOF ? -1 : ch;\n"
    "}\n"
    "\n"
    "static PL0_UNUSED void pl0_write_int(long long v) { printf(\"%lld\\n\", v); }\n"
    "\n"
    "static PL0_UNUSED void pl0_write_char(long long v, int line) {\n"
    "    if (v < 0 || v > 255) pl0_fail(line, \"writeChar: value out of range 0..255\");\n"
    "    putchar((int)v);\n"
    "}\n"
    "\n"
    "/* Arrays double as strings: print characters up to the first 0. */\n"
    "static PL0_UNUSED void pl0_write_arr(const long long *a, long long n, int line) {\n"
    "    for (long long i = 0; i < n && a[i] != 0; i++) pl0_write_char(a[i], line);\n"
    "}\n"
    "\n"
    "static PL0_UNUSED void pl0_exit(long long code) {\n"
    "    fflush(stdout);\n"
    "    exit((int)(code & 0xff));\n"
    "}\n";

int pl0_compile(const char *src, size_t len, Buf *out, char *err, size_t errlen) {
    /* On the heap: after longjmp, non-volatile locals changed since setjmp are
     * indeterminate, but memory reached through an unchanged pointer is fine. */
    Compiler *c = calloc(1, sizeof *c);
    if (!c) {
        snprintf(err, errlen, "line 0: out of memory");
        return -1;
    }
    c->out = out;
    c->err = err;
    c->errlen = errlen;
    c->next_id = 1;
    lexer_init(&c->lx, src, len);

    volatile int rc = -1;
    if (setjmp(c->fail) == 0) {
        emit(c, "%s", runtime);
        next(c);
        block(c, 0, -1, NULL);
        expect(c, T_DOT);
        if (c->tok.kind != T_EOF)
            error_at(c, c->tok.line, "unexpected %s after the final '.'", token_name(c->tok.kind));
        rc = 0;
    }

    for (size_t i = 0; i < c->nstrs; i++) free(c->strs[i]);
    free(c->strs);
    free(c->syms);
    lexer_free(&c->lx);
    free(c);
    return rc;
}
