#include <stdio.h>
#include <stdlib.h>

#include "builtins.h"
#include "lenv.h"
#include "lval.h"
#include "parser.h"

static void eval_print_program(Lenv* env, const char* input) {
    Lval* program = parser_read(input);
    while (program->count > 0) {
        Lval* x = lval_eval(env, lval_pop(program, 0));
        if (!(x->type == LVAL_SEXPR && x->count == 0)) {
            lval_println(x);
        }
        lval_del(x);
    }
    lval_del(program);
}

static void run_file(Lenv* env, const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Could not open %s\n", path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* buf = malloc((size_t)len + 1);
    size_t nread = fread(buf, 1, (size_t)len, f);
    buf[nread] = '\0';
    fclose(f);

    eval_print_program(env, buf);
    free(buf);
}

static void run_repl(Lenv* env) {
    char line[4096];
    printf("Lispy Version 0.1 (build-your-own-lisp, hand-rolled parser)\n");
    printf("Press Ctrl+D to exit\n\n");
    for (;;) {
        printf("lispy> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");
            break;
        }
        eval_print_program(env, line);
    }
}

int main(int argc, char** argv) {
    Lenv* env = lenv_new(NULL);
    lenv_add_builtins(env);

    if (argc >= 2) {
        for (int i = 1; i < argc; i++) {
            run_file(env, argv[i]);
        }
    } else {
        run_repl(env);
    }

    lenv_free_root(env);
    return 0;
}
