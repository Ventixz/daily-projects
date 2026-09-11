#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "shell.h"

static void print_prompt(void) {
    char cwd[4096];
    if (getcwd(cwd, sizeof(cwd))) {
        printf("shell:%s$ ", cwd);
    } else {
        printf("shell$ ");
    }
    fflush(stdout);
}

int main(void) {
    /* Non-interactive mode (piped stdin, as the test suite uses) skips the
     * prompt so scripted input and expected-output fixtures stay easy to diff. */
    int interactive = isatty(STDIN_FILENO);
    int status = 1;

    do {
        if (interactive) print_prompt();

        char *line = sh_read_line();
        if (!line) {
            if (interactive) printf("\n");
            break; /* EOF */
        }

        int token_count = 0;
        char **tokens = sh_tokenize(line, &token_count);
        free(line);

        if (token_count > 0) {
            sh_pipeline_t *pipeline = sh_parse_pipeline(tokens, token_count);
            status = sh_execute_pipeline(pipeline);
            sh_free_pipeline(pipeline);
        }

        for (int i = 0; i < token_count; i++) free(tokens[i]);
        free(tokens);
    } while (status);

    return EXIT_SUCCESS;
}
