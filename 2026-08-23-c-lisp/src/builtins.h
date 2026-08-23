#ifndef BUILTINS_H
#define BUILTINS_H

#include "lenv.h"
#include "lval.h"

Lval* lval_eval(Lenv* env, Lval* v);
Lval* lval_eval_sexpr(Lenv* env, Lval* v);
Lval* lval_call(Lenv* env, Lval* f, Lval* a);

void lenv_add_builtins(Lenv* env);

#endif
