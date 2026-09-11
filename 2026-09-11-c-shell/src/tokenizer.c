#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "shell.h"

char *sh_read_line(void) {
    size_t bufsize = 0;
    char *line = NULL;

    if (getline(&line, &bufsize, stdin) == -1) {
        if (feof(stdin)) {
            free(line);
            return NULL; /* clean EOF, e.g. Ctrl-D */
        }
        perror("shell: getline");
        free(line);
        exit(EXIT_FAILURE);
    }
    return line;
}

/* Splits a line into tokens, honoring single/double quotes as grouping
 * (quote characters themselves are stripped from the token). Operators
 * (|, <, >, >>) must be surrounded by whitespace, same simplification
 * most teaching shells make - see LEARNING.md. */
char **sh_tokenize(char *line, int *out_count) {
    int bufsize = SH_TOK_BUFSIZE;
    int position = 0;
    char **tokens = malloc(bufsize * sizeof(char *));
    if (!tokens) {
        fprintf(stderr, "shell: allocation error\n");
        exit(EXIT_FAILURE);
    }

    size_t len = strlen(line);
    size_t i = 0;
    while (i < len) {
        while (i < len && (line[i] == ' ' || line[i] == '\t' ||
                            line[i] == '\r' || line[i] == '\n')) {
            i++;
        }
        if (i >= len) break;

        char quote = 0;
        if (line[i] == '\'' || line[i] == '"') {
            quote = line[i];
            i++;
        }

        size_t start = i;
        char token_buf[4096];
        size_t tb = 0;

        if (quote) {
            while (i < len && line[i] != quote) {
                token_buf[tb++] = line[i++];
            }
            if (i < len) i++; /* skip closing quote */
        } else {
            while (i < len && line[i] != ' ' && line[i] != '\t' &&
                   line[i] != '\r' && line[i] != '\n') {
                token_buf[tb++] = line[i++];
            }
        }
        (void)start;
        token_buf[tb] = '\0';

        tokens[position] = strdup(token_buf);
        position++;

        if (position >= bufsize) {
            bufsize += SH_TOK_BUFSIZE;
            tokens = realloc(tokens, bufsize * sizeof(char *));
            if (!tokens) {
                fprintf(stderr, "shell: allocation error\n");
                exit(EXIT_FAILURE);
            }
        }
    }

    tokens[position] = NULL;
    *out_count = position;
    return tokens;
}
