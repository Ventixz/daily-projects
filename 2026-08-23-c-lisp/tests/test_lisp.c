#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../src/builtins.h"
#include "../src/lenv.h"
#include "../src/lval.h"
#include "../src/parser.h"

static Lval* eval_str(Lenv* env, const char* src) {
    Lval* program = parser_read(src);
    Lval* result = NULL;
    while (program->count > 0) {
        if (result) lval_del(result);
        result = lval_eval(env, lval_pop(program, 0));
    }
    lval_del(program);
    return result;
}

static void discard(Lenv* env, const char* src) {
    lval_del(eval_str(env, src));
}

/* --- parser edge cases --- */

static void test_parse_empty_sexpr(void) {
    Lval* program = parser_read("()");
    assert(program->count == 1);
    assert(program->cell[0]->type == LVAL_SEXPR);
    assert(program->cell[0]->count == 0);
    lval_del(program);
    printf("PASS test_parse_empty_sexpr\n");
}

static void test_parse_nested_braces(void) {
    Lval* program = parser_read("{{1 2} {3 4}}");
    assert(program->count == 1);
    Lval* outer = program->cell[0];
    assert(outer->type == LVAL_QEXPR);
    assert(outer->count == 2);
    assert(outer->cell[0]->type == LVAL_QEXPR);
    assert(outer->cell[0]->count == 2);
    assert(outer->cell[0]->cell[0]->num == 1);
    assert(outer->cell[1]->cell[1]->num == 4);
    lval_del(program);
    printf("PASS test_parse_nested_braces\n");
}

static void test_parse_trailing_garbage_is_error(void) {
    Lval* program = parser_read("(+ 1 2))");
    assert(program->count == 1);
    assert(program->cell[0]->type == LVAL_ERR);
    lval_del(program);
    printf("PASS test_parse_trailing_garbage_is_error\n");
}

static void test_parse_unmatched_open_paren_is_error(void) {
    Lval* program = parser_read("(+ 1 2");
    assert(program->count == 1);
    assert(program->cell[0]->type == LVAL_ERR);
    lval_del(program);
    printf("PASS test_parse_unmatched_open_paren_is_error\n");
}

static void test_parse_multiple_top_level_forms(void) {
    Lval* program = parser_read("1 2 3");
    assert(program->count == 3);
    assert(program->cell[0]->num == 1);
    assert(program->cell[2]->num == 3);
    lval_del(program);
    printf("PASS test_parse_multiple_top_level_forms\n");
}

static void test_parse_string_and_comment(void) {
    Lval* program = parser_read("; a comment\n\"hi\\nthere\" ; trailing comment");
    assert(program->count == 1);
    assert(program->cell[0]->type == LVAL_STR);
    assert(strcmp(program->cell[0]->str, "hi\nthere") == 0);
    lval_del(program);
    printf("PASS test_parse_string_and_comment\n");
}

/* --- arithmetic + error values --- */

static void test_arithmetic_basic(void) {
    Lenv* env = lenv_new(NULL);
    lenv_add_builtins(env);

    Lval* r = eval_str(env, "(+ 1 2 3)");
    assert(r->type == LVAL_NUM && r->num == 6);
    lval_del(r);

    r = eval_str(env, "(* (+ 2 3) (- 10 4))");
    assert(r->type == LVAL_NUM && r->num == 30);
    lval_del(r);

    r = eval_str(env, "(- 5)");
    assert(r->type == LVAL_NUM && r->num == -5);
    lval_del(r);

    lenv_free_root(env);
    printf("PASS test_arithmetic_basic\n");
}

static void test_division_by_zero_is_error_value(void) {
    Lenv* env = lenv_new(NULL);
    lenv_add_builtins(env);

    Lval* r = eval_str(env, "(/ 1 0)");
    assert(r->type == LVAL_ERR);
    lval_del(r);

    r = eval_str(env, "(+ 1 (/ 1 0) 2)");
    assert(r->type == LVAL_ERR);
    lval_del(r);

    lenv_free_root(env);
    printf("PASS test_division_by_zero_is_error_value\n");
}

static void test_modulo_by_zero_is_error_value(void) {
    Lenv* env = lenv_new(NULL);
    lenv_add_builtins(env);

    Lval* r = eval_str(env, "(% 10 0)");
    assert(r->type == LVAL_ERR);
    lval_del(r);

    r = eval_str(env, "(% 10 3)");
    assert(r->type == LVAL_NUM && r->num == 1);
    lval_del(r);

    lenv_free_root(env);
    printf("PASS test_modulo_by_zero_is_error_value\n");
}

static void test_wrong_arg_count_is_error_value(void) {
    Lenv* env = lenv_new(NULL);
    lenv_add_builtins(env);

    /* Note: "(head)" alone doesn't reach the builtin at all -- a
     * one-element S-expression collapses to that element (the function
     * value itself) before any call happens, matching the tutorial's own
     * quirk that zero-argument calls can't be written this way. So the
     * arg-count check is exercised with a call that still has other
     * elements around it, forcing the actual call to happen. */
    Lval* r = eval_str(env, "(cons 1)");
    assert(r->type == LVAL_ERR);
    lval_del(r);

    r = eval_str(env, "(head {1 2} {3 4})");
    assert(r->type == LVAL_ERR);
    lval_del(r);

    lenv_free_root(env);
    printf("PASS test_wrong_arg_count_is_error_value\n");
}

static void test_wrong_arg_type_is_error_value(void) {
    Lenv* env = lenv_new(NULL);
    lenv_add_builtins(env);

    Lval* r = eval_str(env, "(+ 1 {2})");
    assert(r->type == LVAL_ERR);
    lval_del(r);

    r = eval_str(env, "(head {1 2} 3)");
    assert(r->type == LVAL_ERR);
    lval_del(r);

    lenv_free_root(env);
    printf("PASS test_wrong_arg_type_is_error_value\n");
}

/* --- list operations --- */

static void test_list_operations(void) {
    Lenv* env = lenv_new(NULL);
    lenv_add_builtins(env);

    Lval* r = eval_str(env, "(list 1 2 3)");
    assert(r->type == LVAL_QEXPR && r->count == 3);
    lval_del(r);

    r = eval_str(env, "(head {1 2 3})");
    assert(r->count == 1 && r->cell[0]->num == 1);
    lval_del(r);

    r = eval_str(env, "(tail {1 2 3})");
    assert(r->count == 2 && r->cell[0]->num == 2);
    lval_del(r);

    r = eval_str(env, "(join {1 2} {3 4})");
    assert(r->count == 4 && r->cell[3]->num == 4);
    lval_del(r);

    r = eval_str(env, "(cons 0 {1 2})");
    assert(r->count == 3 && r->cell[0]->num == 0);
    lval_del(r);

    r = eval_str(env, "(len {1 2 3 4})");
    assert(r->num == 4);
    lval_del(r);

    r = eval_str(env, "(init {1 2 3})");
    assert(r->count == 2 && r->cell[1]->num == 2);
    lval_del(r);

    r = eval_str(env, "(eval (head {(+ 1 2) (+ 3 4)}))");
    assert(r->type == LVAL_NUM && r->num == 3);
    lval_del(r);

    lenv_free_root(env);
    printf("PASS test_list_operations\n");
}

/* --- scoping: the classic closure bug --- */

static void test_closure_captures_definition_env(void) {
    Lenv* env = lenv_new(NULL);
    lenv_add_builtins(env);

    discard(env, "(fun {make-adder n} {\\ {x} {+ x n}})");
    discard(env, "(def {add5} (make-adder 5))");
    discard(env, "(def {add10} (make-adder 10))");

    Lval* r5 = eval_str(env, "(add5 100)");
    Lval* r10 = eval_str(env, "(add10 100)");
    assert(r5->type == LVAL_NUM && r5->num == 105);
    assert(r10->type == LVAL_NUM && r10->num == 110);
    assert(r5->num != r10->num);
    lval_del(r5);
    lval_del(r10);

    /* A later global rebinding of the same symbol name used inside the
     * closure must NOT change what the closure sees: add5 must keep using
     * the n=5 it captured lexically at creation time, not resolve 'n'
     * dynamically against whatever is visible at the call site. */
    discard(env, "(def {n} 999)");
    Lval* r5again = eval_str(env, "(add5 1)");
    assert(r5again->type == LVAL_NUM && r5again->num == 6);
    lval_del(r5again);

    lenv_free_root(env);
    printf("PASS test_closure_captures_definition_env\n");
}

static void test_local_put_does_not_leak_to_global(void) {
    Lenv* env = lenv_new(NULL);
    lenv_add_builtins(env);

    discard(env, "(fun {scope-test x} {do (= {y} (* x 2)) (+ x y)})");

    Lval* r = eval_str(env, "(scope-test 5)");
    assert(r->type == LVAL_NUM && r->num == 15);
    lval_del(r);

    /* y was bound with local '=' inside scope-test's own call frame; it
     * must not have escaped into the global environment. */
    Lval* leaked = eval_str(env, "y");
    assert(leaked->type == LVAL_ERR);
    lval_del(leaked);

    lenv_free_root(env);
    printf("PASS test_local_put_does_not_leak_to_global\n");
}

static void test_currying_partial_application(void) {
    Lenv* env = lenv_new(NULL);
    lenv_add_builtins(env);

    discard(env, "(def {add3} (\\ {x y z} {+ x y z}))");
    Lval* partial = eval_str(env, "(add3 1)");
    assert(partial->type == LVAL_FUN);
    lval_del(partial);

    Lval* r = eval_str(env, "((add3 1) 2 3)");
    assert(r->type == LVAL_NUM && r->num == 6);
    lval_del(r);

    r = eval_str(env, "(((add3 1) 2) 3)");
    assert(r->type == LVAL_NUM && r->num == 6);
    lval_del(r);

    lenv_free_root(env);
    printf("PASS test_currying_partial_application\n");
}

/* --- recursion written in the language itself --- */

static void test_recursive_factorial(void) {
    Lenv* env = lenv_new(NULL);
    lenv_add_builtins(env);

    discard(env, "(fun {fact n} {if (== n 0) {1} {* n (fact (- n 1))}})");
    Lval* r = eval_str(env, "(fact 10)");
    assert(r->type == LVAL_NUM && r->num == 3628800);
    lval_del(r);

    lenv_free_root(env);
    printf("PASS test_recursive_factorial\n");
}

static void test_recursive_fibonacci(void) {
    Lenv* env = lenv_new(NULL);
    lenv_add_builtins(env);

    discard(env,
        "(fun {fib n} {if (< n 2) {n} {+ (fib (- n 1)) (fib (- n 2))}})");
    Lval* r = eval_str(env, "(fib 15)");
    assert(r->type == LVAL_NUM && r->num == 610);
    lval_del(r);

    lenv_free_root(env);
    printf("PASS test_recursive_fibonacci\n");
}

static void test_unbound_symbol_is_error_value(void) {
    Lenv* env = lenv_new(NULL);
    lenv_add_builtins(env);

    Lval* r = eval_str(env, "totally-undefined-symbol");
    assert(r->type == LVAL_ERR);
    lval_del(r);

    lenv_free_root(env);
    printf("PASS test_unbound_symbol_is_error_value\n");
}

int main(void) {
    test_parse_empty_sexpr();
    test_parse_nested_braces();
    test_parse_trailing_garbage_is_error();
    test_parse_unmatched_open_paren_is_error();
    test_parse_multiple_top_level_forms();
    test_parse_string_and_comment();

    test_arithmetic_basic();
    test_division_by_zero_is_error_value();
    test_modulo_by_zero_is_error_value();
    test_wrong_arg_count_is_error_value();
    test_wrong_arg_type_is_error_value();

    test_list_operations();

    test_closure_captures_definition_env();
    test_local_put_does_not_leak_to_global();
    test_currying_partial_application();

    test_recursive_factorial();
    test_recursive_fibonacci();

    test_unbound_symbol_is_error_value();

    printf("All tests passed.\n");
    return 0;
}
