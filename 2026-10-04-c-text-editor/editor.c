#define _GNU_SOURCE
#include "editor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void ed_init(Editor *e) {
    memset(e, 0, sizeof *e);
}

void ed_free(Editor *e) {
    for (int i = 0; i < e->numrows; i++) free(e->rows[i].chars);
    free(e->rows);
    ed_init(e);
}

static void insert_row(Editor *e, int at, const char *s, int len) {
    if (at < 0 || at > e->numrows) return;
    e->rows = realloc(e->rows, sizeof(Row) * (e->numrows + 1));
    if (!e->rows) { perror("realloc"); exit(1); }
    memmove(&e->rows[at + 1], &e->rows[at], sizeof(Row) * (e->numrows - at));
    e->rows[at].size = len;
    e->rows[at].chars = malloc(len + 1);
    memcpy(e->rows[at].chars, s, len);
    e->rows[at].chars[len] = '\0';
    e->numrows++;
    e->dirty = 1;
}

static void delete_row(Editor *e, int at) {
    if (at < 0 || at >= e->numrows) return;
    free(e->rows[at].chars);
    memmove(&e->rows[at], &e->rows[at + 1], sizeof(Row) * (e->numrows - at - 1));
    e->numrows--;
    e->dirty = 1;
}

static void row_insert(Row *r, int at, int c) {
    if (at < 0 || at > r->size) at = r->size;
    r->chars = realloc(r->chars, r->size + 2);
    memmove(&r->chars[at + 1], &r->chars[at], r->size - at + 1);
    r->chars[at] = (char)c;
    r->size++;
}

static void row_append(Row *r, const char *s, int len) {
    r->chars = realloc(r->chars, r->size + len + 1);
    memcpy(&r->chars[r->size], s, len);
    r->size += len;
    r->chars[r->size] = '\0';
}

static void row_delete(Row *r, int at) {
    if (at < 0 || at >= r->size) return;
    memmove(&r->chars[at], &r->chars[at + 1], r->size - at);
    r->size--;
}

void ed_insert_char(Editor *e, int c) {
    if (e->cy == e->numrows) insert_row(e, e->numrows, "", 0);  /* typing on the phantom last line */
    row_insert(&e->rows[e->cy], e->cx, c);
    e->cx++;
    e->dirty = 1;
}

void ed_insert_newline(Editor *e) {
    if (e->cy == e->numrows) {
        insert_row(e, e->numrows, "", 0);
    }
    if (e->cx == 0) {
        insert_row(e, e->cy, "", 0);
    } else {
        Row *r = &e->rows[e->cy];
        insert_row(e, e->cy + 1, &r->chars[e->cx], r->size - e->cx);
        r = &e->rows[e->cy];            /* realloc may have moved the array */
        r->size = e->cx;
        r->chars[r->size] = '\0';
    }
    e->cy++;
    e->cx = 0;
}

void ed_backspace(Editor *e) {
    if (e->cy == e->numrows) return;
    if (e->cx == 0 && e->cy == 0) return;
    if (e->cx > 0) {
        row_delete(&e->rows[e->cy], e->cx - 1);
        e->cx--;
    } else {
        e->cx = e->rows[e->cy - 1].size;
        row_append(&e->rows[e->cy - 1], e->rows[e->cy].chars, e->rows[e->cy].size);
        delete_row(e, e->cy);
        e->cy--;
    }
    e->dirty = 1;
}

static int row_len(const Editor *e, int y) {
    return y < e->numrows ? e->rows[y].size : 0;
}

void ed_move_left(Editor *e) {
    if (e->cx > 0) e->cx--;
    else if (e->cy > 0) { e->cy--; e->cx = row_len(e, e->cy); }
}

void ed_move_right(Editor *e) {
    if (e->cy < e->numrows && e->cx < e->rows[e->cy].size) e->cx++;
    else if (e->cy < e->numrows) { e->cy++; e->cx = 0; }
}

static void clamp_cx(Editor *e) {
    int len = row_len(e, e->cy);
    if (e->cx > len) e->cx = len;
}

void ed_move_up(Editor *e) {
    if (e->cy > 0) e->cy--;
    clamp_cx(e);
}

void ed_move_down(Editor *e) {
    if (e->cy < e->numrows) e->cy++;
    clamp_cx(e);
}

int ed_open(Editor *e, const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    while ((n = getline(&line, &cap, fp)) != -1) {
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) n--;
        insert_row(e, e->numrows, line, (int)n);
    }
    free(line);
    fclose(fp);
    e->dirty = 0;
    return 0;
}

char *ed_to_string(const Editor *e, int *len) {
    int total = 0;
    for (int i = 0; i < e->numrows; i++) total += e->rows[i].size + 1;
    char *buf = malloc(total + 1), *p = buf;
    for (int i = 0; i < e->numrows; i++) {
        memcpy(p, e->rows[i].chars, e->rows[i].size);
        p += e->rows[i].size;
        *p++ = '\n';
    }
    *p = '\0';
    if (len) *len = total;
    return buf;
}

int ed_save(Editor *e, const char *path) {
    int len;
    char *buf = ed_to_string(e, &len);
    FILE *fp = fopen(path, "w");
    int ok = fp && fwrite(buf, 1, len, fp) == (size_t)len;
    if (fp && fclose(fp) != 0) ok = 0;
    free(buf);
    if (!ok) return -1;
    e->dirty = 0;
    return len;
}

int ed_find(Editor *e, const char *query) {
    if (!*query || e->numrows == 0) return 0;
    /* Visit every row once, plus the starting row again (to catch matches
     * before the cursor on that row after wrapping). */
    for (int i = 0; i <= e->numrows; i++) {
        int y = (e->cy + i) % e->numrows;
        const char *hay = e->rows[y].chars;
        const char *from = hay;
        if (i == 0) from = hay + (e->cx < e->rows[y].size ? e->cx + 1 : e->rows[y].size);
        const char *hit = strstr(from, query);
        if (i == e->numrows) {            /* back on the start row: only accept hits up to the cursor */
            hit = strstr(hay, query);
            if (hit && hit - hay > e->cx) hit = NULL;
        }
        if (hit) {
            e->cy = y;
            e->cx = (int)(hit - hay);
            return 1;
        }
    }
    return 0;
}
