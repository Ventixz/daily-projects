#ifndef EDITOR_H
#define EDITOR_H
#include <stddef.h>

typedef struct { char *chars; int size; } Row;

typedef struct {
    Row *rows; int numrows;
    int cx, cy;          /* cursor in text coordinates */
    int dirty;
} Editor;

void ed_init(Editor *e);
void ed_free(Editor *e);
void ed_insert_char(Editor *e, int c);
void ed_insert_newline(Editor *e);
void ed_delete_char(Editor *e);          /* backspace */
void ed_move(Editor *e, int dx, int dy); /* clamps to text */
int  ed_open(Editor *e, const char *path);
int  ed_save(Editor *e, const char *path);
char *ed_to_string(const Editor *e, size_t *len); /* caller frees */
int  ed_find(Editor *e, const char *needle, int from_row); /* moves cursor, returns 1 if found */
/* Render rows [top, top+height) clipped to width into a malloc'd string (for the screen). */
char *ed_render(const Editor *e, int top, int height, int width);
#endif
