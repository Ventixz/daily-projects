#ifndef LVAL_H
#define LVAL_H

typedef struct Lenv Lenv;
typedef struct Lval Lval;

typedef enum {
    LVAL_NUM,
    LVAL_ERR,
    LVAL_SYM,
    LVAL_STR,
    LVAL_FUN,
    LVAL_SEXPR,
    LVAL_QEXPR
} LvalType;

typedef Lval* (*Lbuiltin)(Lenv*, Lval*);

struct Lval {
    LvalType type;

    double num;
    char* err;
    char* sym;
    char* str;

    Lbuiltin builtin;
    char* fname;
    Lenv* env;
    Lval* formals;
    Lval* body;

    int count;
    Lval** cell;
};

Lval* lval_num(double x);
Lval* lval_err(const char* fmt, ...);
Lval* lval_sym(const char* s);
Lval* lval_str(const char* s);
Lval* lval_sexpr(void);
Lval* lval_qexpr(void);
Lval* lval_builtin(Lbuiltin func, const char* name);
Lval* lval_lambda(Lenv* env, Lval* formals, Lval* body);

void lval_del(Lval* v);
Lval* lval_copy(const Lval* v);

Lval* lval_add(Lval* v, Lval* x);
Lval* lval_pop(Lval* v, int i);
Lval* lval_take(Lval* v, int i);
Lval* lval_join(Lval* x, Lval* y);

void lval_print(const Lval* v);
void lval_println(const Lval* v);
const char* lval_type_name(LvalType t);
int lval_eq(const Lval* a, const Lval* b);
int lval_is_err(const Lval* v);

#endif
