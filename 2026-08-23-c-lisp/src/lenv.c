#include <stdlib.h>
#include <string.h>

#include "lenv.h"

Lenv* lenv_new(Lenv* parent) {
    Lenv* e = calloc(1, sizeof(Lenv));
    e->parent = parent ? lenv_retain(parent) : NULL;
    e->refcount = 1;
    return e;
}

Lenv* lenv_retain(Lenv* env) {
    if (!env->parent) return env; /* root is immortal, not refcounted */
    env->refcount++;
    return env;
}

void lenv_release(Lenv* env) {
    if (!env) return;
    if (!env->parent) return; /* root: freed only via lenv_free_root */
    env->refcount--;
    if (env->refcount > 0) return;

    for (int i = 0; i < env->count; i++) {
        free(env->syms[i]);
        lval_del(env->vals[i]);
    }
    free(env->syms);
    free(env->vals);
    lenv_release(env->parent);
    free(env);
}

void lenv_free_root(Lenv* root) {
    if (!root) return;
    for (int i = 0; i < root->count; i++) {
        free(root->syms[i]);
        lval_del(root->vals[i]);
    }
    free(root->syms);
    free(root->vals);
    free(root);
}

Lval* lenv_get(Lenv* env, const Lval* k) {
    for (Lenv* e = env; e; e = e->parent) {
        for (int i = 0; i < e->count; i++) {
            if (strcmp(e->syms[i], k->sym) == 0) {
                return lval_copy(e->vals[i]);
            }
        }
    }
    return lval_err("Unbound symbol '%s'", k->sym);
}

void lenv_put(Lenv* env, const Lval* k, Lval* v) {
    for (int i = 0; i < env->count; i++) {
        if (strcmp(env->syms[i], k->sym) == 0) {
            lval_del(env->vals[i]);
            env->vals[i] = lval_copy(v);
            return;
        }
    }

    env->count++;
    env->syms = realloc(env->syms, sizeof(char*) * env->count);
    env->vals = realloc(env->vals, sizeof(Lval*) * env->count);
    env->syms[env->count - 1] = strdup(k->sym);
    env->vals[env->count - 1] = lval_copy(v);
}

void lenv_def(Lenv* env, const Lval* k, Lval* v) {
    Lenv* e = env;
    while (e->parent) e = e->parent;
    lenv_put(e, k, v);
}
