#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lenv.h"
#include "lval.h"

static Lval* lval_new(LvalType type) {
    Lval* v = calloc(1, sizeof(Lval));
    v->type = type;
    return v;
}

Lval* lval_num(double x) {
    Lval* v = lval_new(LVAL_NUM);
    v->num = x;
    return v;
}

Lval* lval_err(const char* fmt, ...) {
    Lval* v = lval_new(LVAL_ERR);
    va_list va;
    va_start(va, fmt);
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, va);
    va_end(va);
    v->err = strdup(buf);
    return v;
}

Lval* lval_sym(const char* s) {
    Lval* v = lval_new(LVAL_SYM);
    v->sym = strdup(s);
    return v;
}

Lval* lval_str(const char* s) {
    Lval* v = lval_new(LVAL_STR);
    v->str = strdup(s);
    return v;
}

Lval* lval_sexpr(void) {
    return lval_new(LVAL_SEXPR);
}

Lval* lval_qexpr(void) {
    return lval_new(LVAL_QEXPR);
}

Lval* lval_builtin(Lbuiltin func, const char* name) {
    Lval* v = lval_new(LVAL_FUN);
    v->builtin = func;
    v->fname = strdup(name);
    return v;
}

Lval* lval_lambda(Lenv* env, Lval* formals, Lval* body) {
    Lval* v = lval_new(LVAL_FUN);
    v->builtin = NULL;
    v->env = lenv_retain(env);
    v->formals = formals;
    v->body = body;
    return v;
}

void lval_del(Lval* v) {
    if (!v) return;

    switch (v->type) {
        case LVAL_NUM:
            break;
        case LVAL_ERR:
            free(v->err);
            break;
        case LVAL_SYM:
            free(v->sym);
            break;
        case LVAL_STR:
            free(v->str);
            break;
        case LVAL_FUN:
            free(v->fname);
            if (!v->builtin) {
                lenv_release(v->env);
                lval_del(v->formals);
                lval_del(v->body);
            }
            break;
        case LVAL_SEXPR:
        case LVAL_QEXPR:
            for (int i = 0; i < v->count; i++) {
                lval_del(v->cell[i]);
            }
            free(v->cell);
            break;
    }

    free(v);
}

Lval* lval_copy(const Lval* v) {
    Lval* x = lval_new(v->type);

    switch (v->type) {
        case LVAL_NUM:
            x->num = v->num;
            break;
        case LVAL_ERR:
            x->err = strdup(v->err);
            break;
        case LVAL_SYM:
            x->sym = strdup(v->sym);
            break;
        case LVAL_STR:
            x->str = strdup(v->str);
            break;
        case LVAL_FUN:
            x->fname = v->fname ? strdup(v->fname) : NULL;
            if (v->builtin) {
                x->builtin = v->builtin;
            } else {
                x->builtin = NULL;
                x->env = lenv_retain(v->env);
                x->formals = lval_copy(v->formals);
                x->body = lval_copy(v->body);
            }
            break;
        case LVAL_SEXPR:
        case LVAL_QEXPR:
            x->count = v->count;
            x->cell = v->count ? malloc(sizeof(Lval*) * v->count) : NULL;
            for (int i = 0; i < v->count; i++) {
                x->cell[i] = lval_copy(v->cell[i]);
            }
            break;
    }

    return x;
}

Lval* lval_add(Lval* v, Lval* x) {
    v->count++;
    v->cell = realloc(v->cell, sizeof(Lval*) * v->count);
    v->cell[v->count - 1] = x;
    return v;
}

Lval* lval_pop(Lval* v, int i) {
    Lval* x = v->cell[i];
    memmove(&v->cell[i], &v->cell[i + 1], sizeof(Lval*) * (v->count - i - 1));
    v->count--;
    if (v->count == 0) {
        free(v->cell);
        v->cell = NULL;
    } else {
        v->cell = realloc(v->cell, sizeof(Lval*) * v->count);
    }
    return x;
}

Lval* lval_take(Lval* v, int i) {
    Lval* x = lval_pop(v, i);
    lval_del(v);
    return x;
}

Lval* lval_join(Lval* x, Lval* y) {
    for (int i = 0; i < y->count; i++) {
        x = lval_add(x, y->cell[i]);
    }
    y->count = 0;
    free(y->cell);
    y->cell = NULL;
    lval_del(y);
    return x;
}

int lval_is_err(const Lval* v) {
    return v->type == LVAL_ERR;
}

const char* lval_type_name(LvalType t) {
    switch (t) {
        case LVAL_NUM: return "Number";
        case LVAL_ERR: return "Error";
        case LVAL_SYM: return "Symbol";
        case LVAL_STR: return "String";
        case LVAL_FUN: return "Function";
        case LVAL_SEXPR: return "S-Expression";
        case LVAL_QEXPR: return "Q-Expression";
    }
    return "Unknown";
}

static void lval_print_num(double n) {
    if (isfinite(n) && n == floor(n) && fabs(n) < 1e15) {
        printf("%lld", (long long)n);
    } else {
        printf("%g", n);
    }
}

static void lval_print_str(const char* s) {
    putchar('"');
    for (const char* p = s; *p; p++) {
        switch (*p) {
            case '"': printf("\\\""); break;
            case '\\': printf("\\\\"); break;
            case '\n': printf("\\n"); break;
            case '\t': printf("\\t"); break;
            case '\r': printf("\\r"); break;
            default: putchar(*p);
        }
    }
    putchar('"');
}

static void lval_print_expr(const Lval* v, char open, char close) {
    putchar(open);
    for (int i = 0; i < v->count; i++) {
        lval_print(v->cell[i]);
        if (i != v->count - 1) putchar(' ');
    }
    putchar(close);
}

void lval_print(const Lval* v) {
    switch (v->type) {
        case LVAL_NUM:
            lval_print_num(v->num);
            break;
        case LVAL_ERR:
            printf("Error: %s", v->err);
            break;
        case LVAL_SYM:
            printf("%s", v->sym);
            break;
        case LVAL_STR:
            lval_print_str(v->str);
            break;
        case LVAL_FUN:
            if (v->builtin) {
                printf("<builtin: %s>", v->fname ? v->fname : "?");
            } else {
                printf("(\\ ");
                lval_print(v->formals);
                putchar(' ');
                lval_print(v->body);
                putchar(')');
            }
            break;
        case LVAL_SEXPR:
            lval_print_expr(v, '(', ')');
            break;
        case LVAL_QEXPR:
            lval_print_expr(v, '{', '}');
            break;
    }
}

void lval_println(const Lval* v) {
    lval_print(v);
    putchar('\n');
}

int lval_eq(const Lval* a, const Lval* b) {
    if (a->type != b->type) return 0;

    switch (a->type) {
        case LVAL_NUM: return a->num == b->num;
        case LVAL_ERR: return strcmp(a->err, b->err) == 0;
        case LVAL_SYM: return strcmp(a->sym, b->sym) == 0;
        case LVAL_STR: return strcmp(a->str, b->str) == 0;
        case LVAL_FUN:
            if (a->builtin || b->builtin) return a->builtin == b->builtin;
            return lval_eq(a->formals, b->formals) && lval_eq(a->body, b->body);
        case LVAL_SEXPR:
        case LVAL_QEXPR:
            if (a->count != b->count) return 0;
            for (int i = 0; i < a->count; i++) {
                if (!lval_eq(a->cell[i], b->cell[i])) return 0;
            }
            return 1;
    }
    return 0;
}
