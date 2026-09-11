#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "shell.h"

static int sh_cd(char **argv);
static int sh_help(char **argv);
static int sh_exit(char **argv);
static int sh_pwd(char **argv);

static const char *builtin_names[] = {"cd", "help", "exit", "pwd"};
typedef int (*builtin_fn)(char **);
static builtin_fn builtin_fns[] = {sh_cd, sh_help, sh_exit, sh_pwd};
static const int builtin_count = sizeof(builtin_names) / sizeof(char *);

int sh_builtin_lookup(const char *name) {
    for (int i = 0; i < builtin_count; i++) {
        if (strcmp(name, builtin_names[i]) == 0) return i;
    }
    return -1;
}

int sh_builtin_run(int index, char **argv) {
    return builtin_fns[index](argv);
}

static int sh_cd(char **argv) {
    const char *target = argv[1];
    if (!target) {
        target = getenv("HOME");
        if (!target) {
            fprintf(stderr, "shell: cd: HOME not set\n");
            return 1;
        }
    }
    if (chdir(target) != 0) {
        fprintf(stderr, "shell: cd: %s: %s\n", target, strerror(errno));
    }
    return 1;
}

static int sh_help(char **argv) {
    (void)argv;
    printf("A tiny Unix shell, built from \"Write a Shell in C\".\n");
    printf("Built-in commands:\n");
    for (int i = 0; i < builtin_count; i++) {
        printf("  %s\n", builtin_names[i]);
    }
    printf("Anything else is looked up on PATH via execvp.\n");
    printf("Supports pipelines (a | b | c) and redirection (<, >, >>).\n");
    fflush(stdout);
    return 1;
}

static int sh_exit(char **argv) {
    (void)argv;
    return 0;
}

static int sh_pwd(char **argv) {
    (void)argv;
    char cwd[4096];
    if (getcwd(cwd, sizeof(cwd))) {
        printf("%s\n", cwd);
        fflush(stdout);
    } else {
        perror("shell: pwd");
    }
    return 1;
}
