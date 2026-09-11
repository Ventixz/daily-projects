#ifndef SHELL_H
#define SHELL_H

#define SH_TOK_BUFSIZE 64
#define SH_TOK_DELIM " \t\r\n\a"

/* A single command: argv is NULL-terminated, ready for execvp.
 * infile/outfile are NULL when no redirection was given. */
typedef struct {
    char **argv;
    char *infile;
    char *outfile;
    int append; /* 1 for >>, 0 for > */
} sh_command_t;

/* A pipeline is N commands connected by '|'. */
typedef struct {
    sh_command_t *commands;
    int count;
} sh_pipeline_t;

char *sh_read_line(void);
char **sh_tokenize(char *line, int *out_count);
sh_pipeline_t *sh_parse_pipeline(char **tokens, int token_count);
void sh_free_pipeline(sh_pipeline_t *pipeline);
int sh_execute_pipeline(sh_pipeline_t *pipeline);

/* Built-ins. Each returns 1 to keep looping, 0 to request shell exit. */
int sh_builtin_lookup(const char *name);
int sh_builtin_run(int index, char **argv);

#endif
