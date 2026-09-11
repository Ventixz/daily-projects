#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "shell.h"

static sh_command_t parse_segment(char **tokens, int start, int end) {
    sh_command_t cmd = {0};
    cmd.argv = malloc((size_t)(end - start + 1) * sizeof(char *));
    int argc = 0;

    for (int i = start; i < end; i++) {
        if (strcmp(tokens[i], "<") == 0) {
            if (i + 1 >= end) {
                fprintf(stderr, "shell: syntax error: expected file after '<'\n");
                continue;
            }
            cmd.infile = strdup(tokens[++i]);
        } else if (strcmp(tokens[i], ">") == 0) {
            if (i + 1 >= end) {
                fprintf(stderr, "shell: syntax error: expected file after '>'\n");
                continue;
            }
            cmd.outfile = strdup(tokens[++i]);
            cmd.append = 0;
        } else if (strcmp(tokens[i], ">>") == 0) {
            if (i + 1 >= end) {
                fprintf(stderr, "shell: syntax error: expected file after '>>'\n");
                continue;
            }
            cmd.outfile = strdup(tokens[++i]);
            cmd.append = 1;
        } else {
            cmd.argv[argc++] = strdup(tokens[i]);
        }
    }
    cmd.argv[argc] = NULL;
    return cmd;
}

sh_pipeline_t *sh_parse_pipeline(char **tokens, int token_count) {
    sh_pipeline_t *pipeline = malloc(sizeof(sh_pipeline_t));
    int max_commands = token_count + 1;
    pipeline->commands = malloc((size_t)max_commands * sizeof(sh_command_t));
    pipeline->count = 0;

    int seg_start = 0;
    for (int i = 0; i <= token_count; i++) {
        if (i == token_count || strcmp(tokens[i], "|") == 0) {
            if (i == seg_start) {
                fprintf(stderr, "shell: syntax error: empty command in pipeline\n");
            } else {
                pipeline->commands[pipeline->count++] =
                    parse_segment(tokens, seg_start, i);
            }
            seg_start = i + 1;
        }
    }

    return pipeline;
}

void sh_free_pipeline(sh_pipeline_t *pipeline) {
    if (!pipeline) return;
    for (int i = 0; i < pipeline->count; i++) {
        sh_command_t *c = &pipeline->commands[i];
        for (int j = 0; c->argv[j]; j++) free(c->argv[j]);
        free(c->argv);
        free(c->infile);
        free(c->outfile);
    }
    free(pipeline->commands);
    free(pipeline);
}
