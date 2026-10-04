#ifndef EDITOR_H
#define EDITOR_H

/* Terminal-independent editor core: a text buffer and the edits made to it.
 * Kept separate from the UI (kilo.c) so it can be unit tested without a tty. */

typedef struct {
    char *chars;
    int size;
} Row;

typedef struct {
    Row *rows;
    int numrows;
    int cx, cy;   /* cursor: column in row, row index */
    int dirty;    /* unsaved modifications */
} Editor;

void ed_init(Editor *e);
void ed_free(Editor *e);

void ed_insert_char(Editor *e, int c);
void ed_insert_newline(Editor *e);
void ed_backspace(Editor *e);          /* delete char left of cursor, joining lines at col 0 */

void ed_move_left(Editor *e);
void ed_move_right(Editor *e);
void ed_move_up(Editor *e);
void ed_move_down(Editor *e);

int ed_open(Editor *e, const char *path);   /* 0 on success, -1 on error */
int ed_save(Editor *e, const char *path);   /* bytes written, -1 on error */
char *ed_to_string(const Editor *e, int *len); /* malloc'd, rows joined by '\n' */

/* Search forward from just after the cursor, wrapping around. On a hit, moves the
 * cursor to the match and returns 1; otherwise returns 0 and leaves it alone. */
int ed_find(Editor *e, const char *query);

#endif
