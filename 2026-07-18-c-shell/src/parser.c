#define _POSIX_C_SOURCE 200809L

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
            return NULL; /* Ctrl-D */
        }
        perror("readline");
        free(line);
        exit(EXIT_FAILURE);
    }
    return line;
}

/* Tokenize one command segment (no '|' in it) into argv, pulling any
 * <, >, >> redirection targets out of the token stream as they're seen. */
static char **sh_tokenize_command(char *segment, command_t *cmd) {
    int bufsize = SH_TOK_BUFSIZE;
    int position = 0;
    char **tokens = malloc(bufsize * sizeof(char *));
    char *token;

    if (!tokens) {
        fprintf(stderr, "shell: allocation error\n");
        exit(EXIT_FAILURE);
    }

    cmd->infile = NULL;
    cmd->outfile = NULL;
    cmd->append = 0;

    token = strtok(segment, SH_TOK_DELIM);
    while (token != NULL) {
        if (strcmp(token, "<") == 0) {
            token = strtok(NULL, SH_TOK_DELIM);
            if (!token) {
                fprintf(stderr, "shell: expected filename after '<'\n");
                free(tokens);
                return NULL;
            }
            cmd->infile = token;
        } else if (strcmp(token, ">>") == 0) {
            token = strtok(NULL, SH_TOK_DELIM);
            if (!token) {
                fprintf(stderr, "shell: expected filename after '>>'\n");
                free(tokens);
                return NULL;
            }
            cmd->outfile = token;
            cmd->append = 1;
        } else if (strcmp(token, ">") == 0) {
            token = strtok(NULL, SH_TOK_DELIM);
            if (!token) {
                fprintf(stderr, "shell: expected filename after '>'\n");
                free(tokens);
                return NULL;
            }
            cmd->outfile = token;
            cmd->append = 0;
        } else {
            tokens[position++] = token;
            if (position >= bufsize) {
                bufsize += SH_TOK_BUFSIZE;
                tokens = realloc(tokens, bufsize * sizeof(char *));
                if (!tokens) {
                    fprintf(stderr, "shell: allocation error\n");
                    exit(EXIT_FAILURE);
                }
            }
        }
        token = strtok(NULL, SH_TOK_DELIM);
    }
    tokens[position] = NULL;
    return tokens;
}

/* Split a full input line on '|' into segments, then tokenize each segment
 * into its own command_t. Returns 0 on success, -1 on a parse error. */
int sh_parse_pipeline(char *line, pipeline_t *pipeline) {
    pipeline->count = 0;
    char *saveptr;
    char *segment = strtok_r(line, "|", &saveptr);

    while (segment != NULL) {
        if (pipeline->count >= SH_MAX_PIPELINE) {
            fprintf(stderr, "shell: too many piped commands (max %d)\n", SH_MAX_PIPELINE);
            return -1;
        }
        command_t *cmd = &pipeline->commands[pipeline->count];
        cmd->argv = sh_tokenize_command(segment, cmd);
        if (cmd->argv == NULL) {
            return -1;
        }
        if (cmd->argv[0] != NULL) {
            pipeline->count++;
        }
        segment = strtok_r(NULL, "|", &saveptr);
    }
    return 0;
}

void sh_free_pipeline(pipeline_t *pipeline) {
    for (int i = 0; i < pipeline->count; i++) {
        free(pipeline->commands[i].argv);
    }
}
