#ifndef SHELL_H
#define SHELL_H

#define SH_TOK_BUFSIZE 64
#define SH_TOK_DELIM " \t\r\n\a"
#define SH_MAX_PIPELINE 16

/* One command within a pipeline: argv plus any redirections it owns. */
typedef struct {
    char **argv;
    char *infile;
    char *outfile;
    int append; /* 1 if outfile was opened with >> */
} command_t;

typedef struct {
    command_t commands[SH_MAX_PIPELINE];
    int count;
} pipeline_t;

/* parser.c */
char *sh_read_line(void);
int sh_parse_pipeline(char *line, pipeline_t *pipeline);
void sh_free_pipeline(pipeline_t *pipeline);

/* builtins.c */
int sh_is_builtin(const char *name);
int sh_run_builtin(char **argv);

/* exec.c */
int sh_run_pipeline(pipeline_t *pipeline);

#endif
