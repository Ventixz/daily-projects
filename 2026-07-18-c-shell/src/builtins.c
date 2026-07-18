#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "shell.h"

static int sh_cd(char **argv) {
    const char *target = argv[1];
    if (target == NULL) {
        target = getenv("HOME");
        if (target == NULL) {
            fprintf(stderr, "shell: cd: HOME not set\n");
            return 1;
        }
    }
    if (chdir(target) != 0) {
        perror("shell: cd");
        return 1;
    }
    return 0;
}

static int sh_help(char **argv) {
    (void)argv;
    printf("A tiny Unix shell, built from project-based-learning's \"Write a Shell in C\".\n");
    printf("Built-in commands:\n");
    printf("  cd [dir]     change directory (defaults to $HOME)\n");
    printf("  pwd          print the working directory\n");
    printf("  help         show this message\n");
    printf("  exit [code]  exit the shell\n");
    printf("Anything else is looked up on $PATH and run as a child process.\n");
    printf("Supports pipelines (a | b | c) and redirection (<, >, >>).\n");
    return 0;
}

static int sh_pwd(char **argv) {
    (void)argv;
    char cwd[4096];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("shell: pwd");
        return 1;
    }
    printf("%s\n", cwd);
    return 0;
}

static int sh_exit(char **argv) {
    int code = 0;
    if (argv[1] != NULL) {
        code = atoi(argv[1]);
    }
    exit(code);
}

int sh_is_builtin(const char *name) {
    return strcmp(name, "cd") == 0 ||
           strcmp(name, "help") == 0 ||
           strcmp(name, "pwd") == 0 ||
           strcmp(name, "exit") == 0;
}

int sh_run_builtin(char **argv) {
    if (strcmp(argv[0], "cd") == 0) return sh_cd(argv);
    if (strcmp(argv[0], "help") == 0) return sh_help(argv);
    if (strcmp(argv[0], "pwd") == 0) return sh_pwd(argv);
    if (strcmp(argv[0], "exit") == 0) return sh_exit(argv);
    return 1; /* unreachable if sh_is_builtin gated the call */
}
