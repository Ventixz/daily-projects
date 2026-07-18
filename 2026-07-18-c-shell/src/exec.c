#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include "shell.h"

static int apply_redirections(command_t *cmd) {
    if (cmd->infile) {
        int fd = open(cmd->infile, O_RDONLY);
        if (fd < 0) {
            perror(cmd->infile);
            return -1;
        }
        dup2(fd, STDIN_FILENO);
        close(fd);
    }
    if (cmd->outfile) {
        int flags = O_WRONLY | O_CREAT | (cmd->append ? O_APPEND : O_TRUNC);
        int fd = open(cmd->outfile, flags, 0644);
        if (fd < 0) {
            perror(cmd->outfile);
            return -1;
        }
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }
    return 0;
}

/* Run a pipeline of 1+ commands connected by pipes, each stage possibly with
 * its own <, >, >> redirection. A lone builtin runs in-process (so "cd" can
 * actually change the shell's directory); everything else is forked. */
int sh_run_pipeline(pipeline_t *pipeline) {
    int n = pipeline->count;
    if (n == 0) return 0;

    if (n == 1 && sh_is_builtin(pipeline->commands[0].argv[0])) {
        return sh_run_builtin(pipeline->commands[0].argv);
    }

    pid_t pids[SH_MAX_PIPELINE];
    int in_fd = -1; /* read end inherited from the previous stage */

    for (int i = 0; i < n; i++) {
        int pipefd[2] = {-1, -1};
        int has_next = (i < n - 1);
        if (has_next && pipe(pipefd) == -1) {
            perror("shell: pipe");
            return 1;
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("shell: fork");
            return 1;
        }

        if (pid == 0) {
            if (in_fd != -1) {
                dup2(in_fd, STDIN_FILENO);
                close(in_fd);
            }
            if (has_next) {
                close(pipefd[0]);
                dup2(pipefd[1], STDOUT_FILENO);
                close(pipefd[1]);
            }

            command_t *cmd = &pipeline->commands[i];
            if (apply_redirections(cmd) != 0) {
                exit(EXIT_FAILURE);
            }

            signal(SIGINT, SIG_DFL);

            if (sh_is_builtin(cmd->argv[0])) {
                exit(sh_run_builtin(cmd->argv));
            }
            execvp(cmd->argv[0], cmd->argv);
            fprintf(stderr, "shell: %s: command not found\n", cmd->argv[0]);
            exit(127);
        }

        pids[i] = pid;
        if (in_fd != -1) close(in_fd);
        if (has_next) {
            close(pipefd[1]);
            in_fd = pipefd[0];
        }
    }

    int status = 0;
    for (int i = 0; i < n; i++) {
        int st;
        waitpid(pids[i], &st, 0);
        if (i == n - 1) status = st;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}
