#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "shell.h"

/* The prompt goes to stderr, not stdout, so piping/redirecting a shell
 * session's output doesn't capture prompt text mixed in with real output. */
static void print_prompt(void) {
    char cwd[4096];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        fprintf(stderr, "myshell:%s$ ", cwd);
    } else {
        fprintf(stderr, "myshell$ ");
    }
    fflush(stderr);
}

int main(void) {
    /* Ctrl-C should interrupt whatever the shell is running, not the shell
     * itself; children reset this to SIG_DFL before exec. */
    signal(SIGINT, SIG_IGN);

    int status = 0;
    do {
        print_prompt();
        char *line = sh_read_line();
        if (line == NULL) {
            printf("\n");
            break; /* EOF (Ctrl-D) */
        }

        pipeline_t pipeline;
        if (sh_parse_pipeline(line, &pipeline) == 0 && pipeline.count > 0) {
            status = sh_run_pipeline(&pipeline);
            sh_free_pipeline(&pipeline);
        }
        free(line);
    } while (1);

    return status;
}
