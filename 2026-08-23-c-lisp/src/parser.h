#ifndef PARSER_H
#define PARSER_H

#include "lval.h"

/* Parses the whole input as a sequence of top-level forms and returns them
 * wrapped in an LVAL_SEXPR (evaluate each child independently). A syntax
 * error anywhere (unmatched bracket, unterminated string, stray closing
 * bracket, trailing garbage) makes the whole call return a single-element
 * SEXPR holding one LVAL_ERR describing it -- the caller doesn't need a
 * separate error path, it just evals and prints like any other value. */
Lval* parser_read(const char* input);

#endif
