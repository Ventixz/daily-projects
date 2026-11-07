#include "editor.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void type(Editor *e, const char *s) {
    for (; *s; s++) *s == '\n' ? ed_insert_newline(e) : ed_insert_char(e, *s);
}
static void expect(Editor *e, const char *want) {
    char *got = ed_to_string(e, NULL);
    if (strcmp(got, want)) { fprintf(stderr, "got [%s] want [%s]\n", got, want); abort(); }
    free(got);
}
int main(void) {
    Editor e; ed_init(&e);
    type(&e, "hello\nworld"); expect(&e, "hello\nworld\n");            /* typing + newline */
    for (int i = 0; i < 5; i++) { ed_delete_char(&e); }
    expect(&e, "hello\n\n");
    ed_delete_char(&e); assert(e.cy == 0 && e.cx == 5); expect(&e, "hello\n"); /* join rows */
    e.cx = 2; ed_insert_newline(&e); expect(&e, "he\nllo\n");             /* split row */
    ed_delete_char(&e); expect(&e, "hello\n");
    ed_free(&e); ed_init(&e);
    ed_delete_char(&e); expect(&e, "");                                 /* backspace on empty is a no-op */

    type(&e, "ab\ncd"); ed_move(&e, 0, -1);                              /* cursor clamping */
    assert(e.cy == 0 && e.cx == 2);
    ed_move(&e, 1, 0); assert(e.cy == 1 && e.cx == 0);                  /* wrap right */
    ed_move(&e, -1, 0); assert(e.cy == 0 && e.cx == 2);                 /* wrap left */
    ed_move(&e, 0, -5); assert(e.cy == 0);

    assert(ed_find(&e, "cd", 0) && e.cy == 1 && e.cx == 0);            /* find */
    assert(ed_find(&e, "b", 1) && e.cy == 0 && e.cx == 1);             /* wraps around */
    assert(!ed_find(&e, "zzz", 0));

    char *scr = ed_render(&e, 0, 4, 1);                                 /* width clip, ~ filler */
    assert(!strcmp(scr, "a\nc\n~\n~\n")); free(scr);

    const char *path = "ed_test.txt";                    /* save/open roundtrip */
    assert(e.dirty); assert(ed_save(&e, path) == 0 && !e.dirty);
    Editor f; ed_init(&f); assert(ed_open(&f, path) == 0 && !f.dirty);
    expect(&f, "ab\ncd\n"); assert(f.numrows == 2);
    assert(ed_open(&f, "/nonexistent/x") == -1);
    remove(path); ed_free(&e); ed_free(&f);
    puts("all tests passed");
    return 0;
}
