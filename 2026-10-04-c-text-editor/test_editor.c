#include "editor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void type(Editor *e, const char *s) {
    for (; *s; s++) *s == '\n' ? ed_insert_newline(e) : ed_insert_char(e, *s);
}

static int text_is(Editor *e, const char *want) {
    int n;
    char *s = ed_to_string(e, &n);
    int eq = strcmp(s, want) == 0;
    if (!eq) printf("  got %s\n", s);
    free(s);
    return eq;
}

static void test_typing(void) {
    Editor e; ed_init(&e);
    type(&e, "hello\nworld");
    CHECK(e.numrows == 2);
    CHECK(text_is(&e, "hello\nworld\n"));
    CHECK(e.cy == 1 && e.cx == 5);
    CHECK(e.dirty);
    ed_free(&e);
}

static void test_split_and_join(void) {
    Editor e; ed_init(&e);
    type(&e, "abcdef");
    e.cx = 3;
    ed_insert_newline(&e);                 /* abc | def */
    CHECK(text_is(&e, "abc\ndef\n"));
    CHECK(e.cy == 1 && e.cx == 0);
    ed_backspace(&e);                      /* join back */
    CHECK(text_is(&e, "abcdef\n"));
    CHECK(e.cy == 0 && e.cx == 3);
    e.cx = 0;
    ed_insert_newline(&e);                 /* blank line above */
    CHECK(text_is(&e, "\nabcdef\n"));
    ed_free(&e);
}

static void test_backspace_edges(void) {
    Editor e; ed_init(&e);
    ed_backspace(&e);                      /* empty buffer: no crash */
    type(&e, "ab");
    ed_backspace(&e);
    CHECK(text_is(&e, "a\n"));
    e.cx = 0;
    ed_backspace(&e);                      /* start of file: no-op */
    CHECK(text_is(&e, "a\n"));
    ed_free(&e);
}

static void test_cursor_movement(void) {
    Editor e; ed_init(&e);
    type(&e, "long line\nhi\nthird");
    ed_move_up(&e); ed_move_up(&e);
    e.cx = 9;
    ed_move_down(&e);                      /* clamps to shorter row */
    CHECK(e.cy == 1 && e.cx == 2);
    ed_move_right(&e);                     /* wraps to next line start */
    CHECK(e.cy == 2 && e.cx == 0);
    ed_move_left(&e);                      /* wraps to end of previous */
    CHECK(e.cy == 1 && e.cx == 2);
    ed_free(&e);
}

static void test_save_and_open(void) {
    const char *path = "/tmp/kilo_test_file.txt";
    Editor e; ed_init(&e);
    type(&e, "one\ntwo\n\nfour");
    CHECK(ed_save(&e, path) == 14);
    CHECK(!e.dirty);
    ed_free(&e);

    ed_init(&e);
    CHECK(ed_open(&e, path) == 0);
    CHECK(e.numrows == 4);
    CHECK(strcmp(e.rows[0].chars, "one") == 0);
    CHECK(e.rows[2].size == 0);
    CHECK(!e.dirty);
    CHECK(ed_open(&e, "/nonexistent/file") == -1);
    ed_free(&e);
    remove(path);
}

static void test_find(void) {
    Editor e; ed_init(&e);
    type(&e, "foo bar\nbaz foo\nqux");
    e.cx = 0; e.cy = 0;
    CHECK(ed_find(&e, "foo"));             /* skips the match under the cursor */
    CHECK(e.cy == 1 && e.cx == 4);
    CHECK(ed_find(&e, "foo"));             /* wraps around to the first row */
    CHECK(e.cy == 0 && e.cx == 0);
    CHECK(!ed_find(&e, "zzz"));
    CHECK(e.cy == 0 && e.cx == 0);         /* cursor unchanged on miss */
    CHECK(ed_find(&e, "qux") && e.cy == 2);
    CHECK(!ed_find(&e, ""));
    ed_free(&e);
}

int main(void) {
    test_typing();
    test_split_and_join();
    test_backspace_edges();
    test_cursor_movement();
    test_save_and_open();
    test_find();
    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
