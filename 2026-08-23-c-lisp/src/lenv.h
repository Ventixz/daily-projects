#ifndef LENV_H
#define LENV_H

#include "lval.h"

struct Lenv {
    Lenv* parent;
    int refcount;
    int count;
    char** syms;
    Lval** vals;
};

/* A freshly created env starts with refcount 1, owned by its creator.
 * Anything that keeps a pointer to it past that (a lambda closing over it,
 * a child env's parent link) must call lenv_retain; lenv_release drops one
 * reference and frees the env (and releases its parent) only at refcount 0.
 *
 * The one env this scheme deliberately does NOT refcount is the root
 * (parent == NULL): a function defined with `def`/`fun` at global scope
 * always ends up stored inside the very env it closes over, which is a
 * genuine reference cycle (root -> the function value -> root) that plain
 * refcounting can never collect. Rather than write a cycle collector, the
 * root is instead treated as immortal for the process's lifetime: retain/
 * release are no-ops on it, and its owner frees it exactly once, at
 * shutdown, with lenv_free_root -- bypassing the refcount entirely. */
Lenv* lenv_new(Lenv* parent);
Lenv* lenv_retain(Lenv* env);
void lenv_release(Lenv* env);
void lenv_free_root(Lenv* root);

Lval* lenv_get(Lenv* env, const Lval* k);
void lenv_put(Lenv* env, const Lval* k, Lval* v);
void lenv_def(Lenv* env, const Lval* k, Lval* v);

#endif
