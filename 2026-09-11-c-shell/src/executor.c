#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include "shell.h"

/* Runs one command in the process it's called from: a builtin executes
 * in place, an external program replaces the process image via execvp.
 * Only returns if argv[0] is a builtin - callers in a forked child must
 * _exit() themselves afterward. */
static void exec_or_run_builtin(sh_command_t *cmd, int *builtin_result) {
    if (cmd->argv[0] == NULL) {
        *builtin_result = 1;
        return;
    }

    int idx = sh_builtin_lookup(cmd->argv[0]);
    if (idx >= 0) {
        *builtin_result = sh_builtin_run(idx, cmd->argv);
        return;
    }

    execvp(cmd->argv[0], cmd->argv);
    /* execvp only returns on failure */
    fprintf(stderr, "shell: %s: command not found\n", cmd->argv[0]);
    _exit(127);
}

int sh_execute_pipeline(sh_pipeline_t *pipeline) {
    int num_cmds = pipeline->count;
    if (num_cmds == 0) return 1;

    /* A lone builtin runs in the shell's own process, so `cd` and `exit`
     * actually affect the shell instead of a throwaway child. */
    if (num_cmds == 1) {
        sh_command_t *only = &pipeline->commands[0];
        if (only->argv[0] != NULL && !only->infile && !only->outfile) {
            int idx = sh_builtin_lookup(only->argv[0]);
            if (idx >= 0) return sh_builtin_run(idx, only->argv);
        }
    }

    pid_t *pids = malloc((size_t)num_cmds * sizeof(pid_t));
    int prev_read = -1;

    for (int i = 0; i < num_cmds; i++) {
        sh_command_t *cmd = &pipeline->commands[i];
        int pipefd[2] = {-1, -1};
        if (i < num_cmds - 1 && pipe(pipefd) == -1) {
            perror("shell: pipe");
            exit(EXIT_FAILURE);
        }

        /* Flush anything a builtin already buffered in our stdio (e.g. a
         * previous `pwd`) before a child starts writing to the fd directly -
         * otherwise buffered parent output can land after unbuffered child
         * output even though it happened first. */
        fflush(NULL);

        pid_t pid = fork();
        if (pid < 0) {
            perror("shell: fork");
            exit(EXIT_FAILURE);
        }

        if (pid == 0) {
            if (prev_read != -1) {
                dup2(prev_read, STDIN_FILENO);
                close(prev_read);
            } else if (cmd->infile) {
                int fd = open(cmd->infile, O_RDONLY);
                if (fd < 0) {
                    perror(cmd->infile);
                    _exit(EXIT_FAILURE);
                }
                dup2(fd, STDIN_FILENO);
                close(fd);
            }

            if (i < num_cmds - 1) {
                close(pipefd[0]);
                dup2(pipefd[1], STDOUT_FILENO);
                close(pipefd[1]);
            } else if (cmd->outfile) {
                int flags = O_WRONLY | O_CREAT | (cmd->append ? O_APPEND : O_TRUNC);
                int fd = open(cmd->outfile, flags, 0644);
                if (fd < 0) {
                    perror(cmd->outfile);
                    _exit(EXIT_FAILURE);
                }
                dup2(fd, STDOUT_FILENO);
                close(fd);
            }

            int builtin_result = 1;
            exec_or_run_builtin(cmd, &builtin_result);
            _exit(EXIT_SUCCESS); /* only reached if argv[0] was a builtin */
        }

        /* parent */
        if (prev_read != -1) close(prev_read);
        if (i < num_cmds - 1) {
            close(pipefd[1]);
            prev_read = pipefd[0];
        }
        pids[i] = pid;
    }

    for (int i = 0; i < num_cmds; i++) {
        int wstatus;
        waitpid(pids[i], &wstatus, 0);
    }
    free(pids);
    return 1;
}
