#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "builtins.h"

#define LASSERT(args, cond, ...)                          \
    if (!(cond)) {                                        \
        Lval* lassert_err = lval_err(__VA_ARGS__);         \
        lval_del(args);                                   \
        return lassert_err;                               \
    }

#define LASSERT_TYPE(fname, args, index, expect)                            \
    LASSERT(args, (args)->cell[index]->type == (expect),                    \
        "Function '%s' passed incorrect type for argument %d. Got %s, "    \
        "Expected %s.",                                                    \
        fname, index, lval_type_name((args)->cell[index]->type),           \
        lval_type_name(expect))

#define LASSERT_NUM(fname, args, num)                                       \
    LASSERT(args, (args)->count == (num),                                   \
        "Function '%s' passed incorrect number of arguments. Got %d, "    \
        "Expected %d.",                                                    \
        fname, (args)->count, num)

#define LASSERT_NOT_EMPTY(fname, args, index)                               \
    LASSERT(args, (args)->cell[index]->count != 0,                          \
        "Function '%s' passed {} for argument %d.", fname, index)

Lval* lval_eval(Lenv* env, Lval* v) {
    if (v->type == LVAL_SYM) {
        Lval* x = lenv_get(env, v);
        lval_del(v);
        return x;
    }
    if (v->type == LVAL_SEXPR) {
        return lval_eval_sexpr(env, v);
    }
    return v;
}

Lval* lval_eval_sexpr(Lenv* env, Lval* v) {
    for (int i = 0; i < v->count; i++) {
        v->cell[i] = lval_eval(env, v->cell[i]);
    }
    for (int i = 0; i < v->count; i++) {
        if (v->cell[i]->type == LVAL_ERR) return lval_take(v, i);
    }

    if (v->count == 0) return v;
    if (v->count == 1) return lval_take(v, 0);

    Lval* f = lval_pop(v, 0);
    if (f->type != LVAL_FUN) {
        Lval* err = lval_err(
            "S-Expression starts with incorrect type. Got %s, Expected %s.",
            lval_type_name(f->type), lval_type_name(LVAL_FUN));
        lval_del(f);
        lval_del(v);
        return err;
    }

    Lval* result = lval_call(env, f, v);
    lval_del(f);
    return result;
}

Lval* lval_call(Lenv* env, Lval* f, Lval* a) {
    if (f->builtin) return f->builtin(env, a);

    int given = a->count;
    int total = f->formals->count;

    Lenv* fun_env = lenv_new(f->env);

    while (a->count) {
        if (f->formals->count == 0) {
            lenv_release(fun_env);
            lval_del(a);
            return lval_err(
                "Function passed too many arguments. Got %d, Expected %d.",
                given, total);
        }

        Lval* sym = lval_pop(f->formals, 0);
        Lval* val = lval_pop(a, 0);
        lenv_put(fun_env, sym, val);
        lval_del(sym);
        lval_del(val);
    }
    lval_del(a);

    if (f->formals->count == 0) {
        Lval* body_copy = lval_copy(f->body);
        body_copy->type = LVAL_SEXPR;
        Lval* result = lval_eval(fun_env, body_copy);
        lenv_release(fun_env);
        return result;
    }

    Lval* partial = lval_lambda(fun_env, lval_copy(f->formals), lval_copy(f->body));
    lenv_release(fun_env);
    return partial;
}

static Lval* builtin_op(Lenv* env, Lval* a, const char* op) {
    (void)env;
    for (int i = 0; i < a->count; i++) {
        if (a->cell[i]->type != LVAL_NUM) {
            Lval* err = lval_err(
                "Function '%s' passed incorrect type for argument %d. Got "
                "%s, Expected %s.",
                op, i, lval_type_name(a->cell[i]->type),
                lval_type_name(LVAL_NUM));
            lval_del(a);
            return err;
        }
    }
    LASSERT(a, a->count >= 1, "Function '%s' passed no arguments.", op);

    Lval* x = lval_pop(a, 0);

    if (strcmp(op, "-") == 0 && a->count == 0) {
        x->num = -x->num;
    }

    while (a->count > 0) {
        Lval* y = lval_pop(a, 0);

        if (strcmp(op, "+") == 0) {
            x->num += y->num;
        } else if (strcmp(op, "-") == 0) {
            x->num -= y->num;
        } else if (strcmp(op, "*") == 0) {
            x->num *= y->num;
        } else if (strcmp(op, "/") == 0) {
            if (y->num == 0) {
                lval_del(x);
                lval_del(y);
                lval_del(a);
                return lval_err("Division by zero.");
            }
            x->num /= y->num;
        } else if (strcmp(op, "%") == 0) {
            if (x->num != floor(x->num) || y->num != floor(y->num)) {
                lval_del(x);
                lval_del(y);
                lval_del(a);
                return lval_err("Modulo requires integer operands.");
            }
            long yl = (long)y->num;
            if (yl == 0) {
                lval_del(x);
                lval_del(y);
                lval_del(a);
                return lval_err("Modulo by zero.");
            }
            x->num = (double)((long)x->num % yl);
        } else if (strcmp(op, "^") == 0) {
            x->num = pow(x->num, y->num);
        }

        lval_del(y);
    }

    lval_del(a);
    return x;
}

static Lval* builtin_add(Lenv* e, Lval* a) { return builtin_op(e, a, "+"); }
static Lval* builtin_sub(Lenv* e, Lval* a) { return builtin_op(e, a, "-"); }
static Lval* builtin_mul(Lenv* e, Lval* a) { return builtin_op(e, a, "*"); }
static Lval* builtin_div(Lenv* e, Lval* a) { return builtin_op(e, a, "/"); }
static Lval* builtin_mod(Lenv* e, Lval* a) { return builtin_op(e, a, "%"); }
static Lval* builtin_pow(Lenv* e, Lval* a) { return builtin_op(e, a, "^"); }

static Lval* builtin_ord(Lenv* env, Lval* a, const char* op) {
    (void)env;
    LASSERT_NUM(op, a, 2);
    LASSERT_TYPE(op, a, 0, LVAL_NUM);
    LASSERT_TYPE(op, a, 1, LVAL_NUM);

    double x = a->cell[0]->num;
    double y = a->cell[1]->num;
    int r;
    if (strcmp(op, ">") == 0) r = x > y;
    else if (strcmp(op, "<") == 0) r = x < y;
    else if (strcmp(op, ">=") == 0) r = x >= y;
    else r = x <= y;

    lval_del(a);
    return lval_num(r);
}

static Lval* builtin_gt(Lenv* e, Lval* a) { return builtin_ord(e, a, ">"); }
static Lval* builtin_lt(Lenv* e, Lval* a) { return builtin_ord(e, a, "<"); }
static Lval* builtin_ge(Lenv* e, Lval* a) { return builtin_ord(e, a, ">="); }
static Lval* builtin_le(Lenv* e, Lval* a) { return builtin_ord(e, a, "<="); }

static Lval* builtin_cmp(Lenv* env, Lval* a, const char* op) {
    (void)env;
    LASSERT_NUM(op, a, 2);
    int r = lval_eq(a->cell[0], a->cell[1]);
    if (strcmp(op, "!=") == 0) r = !r;
    lval_del(a);
    return lval_num(r);
}

static Lval* builtin_eq(Lenv* e, Lval* a) { return builtin_cmp(e, a, "=="); }
static Lval* builtin_ne(Lenv* e, Lval* a) { return builtin_cmp(e, a, "!="); }

static Lval* builtin_if(Lenv* env, Lval* a) {
    LASSERT_NUM("if", a, 3);
    LASSERT_TYPE("if", a, 0, LVAL_NUM);
    LASSERT_TYPE("if", a, 1, LVAL_QEXPR);
    LASSERT_TYPE("if", a, 2, LVAL_QEXPR);

    a->cell[1]->type = LVAL_SEXPR;
    a->cell[2]->type = LVAL_SEXPR;

    Lval* result;
    if (a->cell[0]->num != 0) {
        result = lval_eval(env, lval_pop(a, 1));
    } else {
        result = lval_eval(env, lval_pop(a, 2));
    }
    lval_del(a);
    return result;
}

static Lval* builtin_list(Lenv* env, Lval* a) {
    (void)env;
    a->type = LVAL_QEXPR;
    return a;
}

static Lval* builtin_head(Lenv* env, Lval* a) {
    (void)env;
    LASSERT_NUM("head", a, 1);
    LASSERT_TYPE("head", a, 0, LVAL_QEXPR);
    LASSERT_NOT_EMPTY("head", a, 0);

    Lval* v = lval_take(a, 0);
    while (v->count > 1) lval_del(lval_pop(v, 1));
    return v;
}

static Lval* builtin_tail(Lenv* env, Lval* a) {
    (void)env;
    LASSERT_NUM("tail", a, 1);
    LASSERT_TYPE("tail", a, 0, LVAL_QEXPR);
    LASSERT_NOT_EMPTY("tail", a, 0);

    Lval* v = lval_take(a, 0);
    lval_del(lval_pop(v, 0));
    return v;
}

static Lval* builtin_join(Lenv* env, Lval* a) {
    (void)env;
    for (int i = 0; i < a->count; i++) {
        LASSERT_TYPE("join", a, i, LVAL_QEXPR);
    }

    Lval* x = lval_pop(a, 0);
    while (a->count) {
        x = lval_join(x, lval_pop(a, 0));
    }
    lval_del(a);
    return x;
}

static Lval* builtin_eval(Lenv* env, Lval* a) {
    LASSERT_NUM("eval", a, 1);
    LASSERT_TYPE("eval", a, 0, LVAL_QEXPR);

    Lval* x = lval_take(a, 0);
    x->type = LVAL_SEXPR;
    return lval_eval(env, x);
}

static Lval* builtin_cons(Lenv* env, Lval* a) {
    (void)env;
    LASSERT_NUM("cons", a, 2);
    LASSERT_TYPE("cons", a, 1, LVAL_QEXPR);

    Lval* x = lval_qexpr();
    lval_add(x, lval_pop(a, 0));
    Lval* rest = lval_take(a, 0);
    return lval_join(x, rest);
}

static Lval* builtin_len(Lenv* env, Lval* a) {
    (void)env;
    LASSERT_NUM("len", a, 1);
    LASSERT_TYPE("len", a, 0, LVAL_QEXPR);

    Lval* r = lval_num(a->cell[0]->count);
    lval_del(a);
    return r;
}

static Lval* builtin_init(Lenv* env, Lval* a) {
    (void)env;
    LASSERT_NUM("init", a, 1);
    LASSERT_TYPE("init", a, 0, LVAL_QEXPR);
    LASSERT_NOT_EMPTY("init", a, 0);

    Lval* v = lval_take(a, 0);
    lval_del(lval_pop(v, v->count - 1));
    return v;
}

static Lval* builtin_var(Lenv* env, Lval* a, const char* fname) {
    LASSERT_TYPE(fname, a, 0, LVAL_QEXPR);

    Lval* syms = a->cell[0];
    for (int i = 0; i < syms->count; i++) {
        LASSERT(a, syms->cell[i]->type == LVAL_SYM,
            "Function '%s' cannot define non-symbol.", fname);
    }
    LASSERT(a, syms->count == a->count - 1,
        "Function '%s' passed mismatched number of symbols (%d) and values "
        "(%d).",
        fname, syms->count, a->count - 1);

    for (int i = 0; i < syms->count; i++) {
        if (strcmp(fname, "def") == 0) {
            lenv_def(env, syms->cell[i], a->cell[i + 1]);
        } else {
            lenv_put(env, syms->cell[i], a->cell[i + 1]);
        }
    }

    lval_del(a);
    return lval_sexpr();
}

static Lval* builtin_def(Lenv* e, Lval* a) { return builtin_var(e, a, "def"); }
static Lval* builtin_put(Lenv* e, Lval* a) { return builtin_var(e, a, "="); }

static Lval* builtin_lambda(Lenv* env, Lval* a) {
    LASSERT_NUM("\\", a, 2);
    LASSERT_TYPE("\\", a, 0, LVAL_QEXPR);
    LASSERT_TYPE("\\", a, 1, LVAL_QEXPR);

    for (int i = 0; i < a->cell[0]->count; i++) {
        LASSERT(a, a->cell[0]->cell[i]->type == LVAL_SYM,
            "Cannot define non-symbol argument. Got %s, Expected %s.",
            lval_type_name(a->cell[0]->cell[i]->type),
            lval_type_name(LVAL_SYM));
    }

    Lval* formals = lval_pop(a, 0);
    Lval* body = lval_pop(a, 0);
    lval_del(a);

    return lval_lambda(env, formals, body);
}

static Lval* builtin_fun(Lenv* env, Lval* a) {
    LASSERT_NUM("fun", a, 2);
    LASSERT_TYPE("fun", a, 0, LVAL_QEXPR);
    LASSERT_TYPE("fun", a, 1, LVAL_QEXPR);
    LASSERT_NOT_EMPTY("fun", a, 0);
    LASSERT(a, a->cell[0]->cell[0]->type == LVAL_SYM,
        "Function 'fun' cannot define non-symbol name.");

    Lval* header = lval_pop(a, 0);
    Lval* body = lval_pop(a, 0);
    lval_del(a);

    Lval* name = lval_pop(header, 0);
    Lval* fn = lval_lambda(env, header, body);
    lenv_def(env, name, fn);
    lval_del(name);
    lval_del(fn);
    return lval_sexpr();
}

static Lval* builtin_do(Lenv* env, Lval* a) {
    (void)env;
    if (a->count == 0) {
        lval_del(a);
        return lval_sexpr();
    }
    Lval* last = lval_pop(a, a->count - 1);
    lval_del(a);
    return last;
}

static Lval* builtin_print(Lenv* env, Lval* a) {
    (void)env;
    for (int i = 0; i < a->count; i++) {
        lval_print(a->cell[i]);
        if (i != a->count - 1) putchar(' ');
    }
    putchar('\n');
    lval_del(a);
    return lval_sexpr();
}

static Lval* builtin_error(Lenv* env, Lval* a) {
    (void)env;
    LASSERT_NUM("error", a, 1);
    LASSERT_TYPE("error", a, 0, LVAL_STR);

    Lval* err = lval_err("%s", a->cell[0]->str);
    lval_del(a);
    return err;
}

static Lval* builtin_and(Lenv* env, Lval* a) {
    (void)env;
    for (int i = 0; i < a->count; i++) LASSERT_TYPE("&&", a, i, LVAL_NUM);
    int r = 1;
    for (int i = 0; i < a->count; i++) {
        if (a->cell[i]->num == 0) r = 0;
    }
    lval_del(a);
    return lval_num(r);
}

static Lval* builtin_or(Lenv* env, Lval* a) {
    (void)env;
    for (int i = 0; i < a->count; i++) LASSERT_TYPE("||", a, i, LVAL_NUM);
    int r = 0;
    for (int i = 0; i < a->count; i++) {
        if (a->cell[i]->num != 0) r = 1;
    }
    lval_del(a);
    return lval_num(r);
}

static Lval* builtin_not(Lenv* env, Lval* a) {
    (void)env;
    LASSERT_NUM("!", a, 1);
    LASSERT_TYPE("!", a, 0, LVAL_NUM);
    int r = a->cell[0]->num == 0;
    lval_del(a);
    return lval_num(r);
}

static void add_builtin(Lenv* env, const char* name, Lbuiltin fn) {
    Lval* k = lval_sym(name);
    Lval* v = lval_builtin(fn, name);
    lenv_put(env, k, v);
    lval_del(k);
    lval_del(v);
}

void lenv_add_builtins(Lenv* env) {
    add_builtin(env, "+", builtin_add);
    add_builtin(env, "-", builtin_sub);
    add_builtin(env, "*", builtin_mul);
    add_builtin(env, "/", builtin_div);
    add_builtin(env, "%", builtin_mod);
    add_builtin(env, "^", builtin_pow);

    add_builtin(env, ">", builtin_gt);
    add_builtin(env, "<", builtin_lt);
    add_builtin(env, ">=", builtin_ge);
    add_builtin(env, "<=", builtin_le);
    add_builtin(env, "==", builtin_eq);
    add_builtin(env, "!=", builtin_ne);

    add_builtin(env, "&&", builtin_and);
    add_builtin(env, "||", builtin_or);
    add_builtin(env, "!", builtin_not);

    add_builtin(env, "if", builtin_if);

    add_builtin(env, "list", builtin_list);
    add_builtin(env, "head", builtin_head);
    add_builtin(env, "tail", builtin_tail);
    add_builtin(env, "join", builtin_join);
    add_builtin(env, "eval", builtin_eval);
    add_builtin(env, "cons", builtin_cons);
    add_builtin(env, "len", builtin_len);
    add_builtin(env, "init", builtin_init);

    add_builtin(env, "def", builtin_def);
    add_builtin(env, "=", builtin_put);
    add_builtin(env, "put", builtin_put);
    add_builtin(env, "\\", builtin_lambda);
    add_builtin(env, "fun", builtin_fun);

    add_builtin(env, "do", builtin_do);
    add_builtin(env, "print", builtin_print);
    add_builtin(env, "error", builtin_error);
}
