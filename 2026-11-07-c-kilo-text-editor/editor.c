#include "editor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void ed_init(Editor *e) { memset(e, 0, sizeof *e); }

void ed_free(Editor *e) {
    for (int i = 0; i < e->numrows; i++) free(e->rows[i].chars);
    free(e->rows);
    ed_init(e);
}

static void insert_row(Editor *e, int at, const char *s, int len) {
    e->rows = realloc(e->rows, sizeof(Row) * (e->numrows + 1));
    memmove(&e->rows[at + 1], &e->rows[at], sizeof(Row) * (e->numrows - at));
    e->rows[at].size = len;
    e->rows[at].chars = malloc(len + 1);
    memcpy(e->rows[at].chars, s, len);
    e->rows[at].chars[len] = '\0';
    e->numrows++;
}

static void delete_row(Editor *e, int at) {
    free(e->rows[at].chars);
    memmove(&e->rows[at], &e->rows[at + 1], sizeof(Row) * (e->numrows - at - 1));
    e->numrows--;
}

void ed_insert_char(Editor *e, int c) {
    if (e->cy == e->numrows) insert_row(e, e->numrows, "", 0);
    Row *r = &e->rows[e->cy];
    r->chars = realloc(r->chars, r->size + 2);
    memmove(&r->chars[e->cx + 1], &r->chars[e->cx], r->size - e->cx + 1);
    r->chars[e->cx] = (char)c;
    r->size++; e->cx++; e->dirty = 1;
}

void ed_insert_newline(Editor *e) {
    if (e->cy == e->numrows) { insert_row(e, e->numrows, "", 0); }
    Row *r = &e->rows[e->cy];
    int tail = r->size - e->cx;
    insert_row(e, e->cy + 1, &r->chars[e->cx], tail);
    r = &e->rows[e->cy];          /* realloc may have moved rows */
    r->size = e->cx; r->chars[r->size] = '\0';
    e->cy++; e->cx = 0; e->dirty = 1;
}

void ed_delete_char(Editor *e) {
    if (e->cy == e->numrows || (e->cx == 0 && e->cy == 0)) return;
    Row *r = &e->rows[e->cy];
    if (e->cx > 0) {
        memmove(&r->chars[e->cx - 1], &r->chars[e->cx], r->size - e->cx + 1);
        r->size--; e->cx--;
    } else { /* join with previous row */
        Row *p = &e->rows[e->cy - 1];
        int old = p->size;
        p->chars = realloc(p->chars, p->size + r->size + 1);
        memcpy(&p->chars[p->size], r->chars, r->size + 1);
        p->size += r->size;
        delete_row(e, e->cy);
        e->cy--; e->cx = old;
    }
    e->dirty = 1;
}

void ed_move(Editor *e, int dx, int dy) {
    e->cy += dy;
    if (e->cy < 0) e->cy = 0;
    if (e->cy > e->numrows) e->cy = e->numrows;
    int len = e->cy < e->numrows ? e->rows[e->cy].size : 0;
    e->cx += dx;
    if (e->cx < 0) {
        if (e->cy > 0) { e->cy--; e->cx = e->rows[e->cy].size; } else e->cx = 0;
    } else if (e->cx > len) {
        if (dx > 0 && e->cy < e->numrows) { e->cy++; e->cx = 0; } else e->cx = len;
    }
    len = e->cy < e->numrows ? e->rows[e->cy].size : 0;
    if (e->cx > len) e->cx = len;
}

int ed_open(Editor *e, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char *line = NULL; size_t cap = 0; ssize_t n;
    while ((n = getline(&line, &cap, f)) != -1) {
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) n--;
        insert_row(e, e->numrows, line, (int)n);
    }
    free(line); fclose(f);
    e->dirty = 0;
    return 0;
}

char *ed_to_string(const Editor *e, size_t *len) {
    size_t total = 0;
    for (int i = 0; i < e->numrows; i++) total += e->rows[i].size + 1;
    char *buf = malloc(total + 1), *p = buf;
    for (int i = 0; i < e->numrows; i++) {
        memcpy(p, e->rows[i].chars, e->rows[i].size); p += e->rows[i].size; *p++ = '\n';
    }
    *p = '\0';
    if (len) *len = total;
    return buf;
}

int ed_save(Editor *e, const char *path) {
    size_t len; char *buf = ed_to_string(e, &len);
    FILE *f = fopen(path, "w");
    int ok = f && fwrite(buf, 1, len, f) == len;
    if (f && fclose(f) != 0) ok = 0;
    free(buf);
    if (ok) e->dirty = 0;
    return ok ? 0 : -1;
}

int ed_find(Editor *e, const char *needle, int from_row) {
    for (int k = 0; k < e->numrows; k++) {
        int i = (from_row + k) % e->numrows;
        char *m = strstr(e->rows[i].chars, needle);
        if (m) { e->cy = i; e->cx = (int)(m - e->rows[i].chars); return 1; }
    }
    return 0;
}

char *ed_render(const Editor *e, int top, int height, int width) {
    size_t cap = (size_t)height * (width + 2) + 1;
    char *out = malloc(cap), *p = out;
    for (int y = 0; y < height; y++) {
        int r = top + y;
        if (r < e->numrows) {
            int n = e->rows[r].size < width ? e->rows[r].size : width;
            memcpy(p, e->rows[r].chars, n); p += n;
        } else *p++ = '~';
        *p++ = '\n';
    }
    *p = '\0';
    return out;
}
