/* No framework: each check prints PASS/FAIL and the run exits non-zero if
 * anything failed, same convention as the rest of this repo's C projects. */
#include "../src/common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (cond) {                                                          \
            printf("PASS: %s\n", msg);                                       \
        } else {                                                             \
            printf("FAIL: %s\n", msg);                                       \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static void test_linebuf_whole_line_at_once(void) {
    linebuf_t lb;
    linebuf_init(&lb);
    linebuf_append(&lb, "ECHO hi\n", 8);
    char *line = linebuf_take_line(&lb);
    CHECK(line != NULL && strcmp(line, "ECHO hi") == 0, "linebuf: one full line extracted whole");
    CHECK(linebuf_take_line(&lb) == NULL, "linebuf: empty after consuming the only line");
    free(line);
    linebuf_free(&lb);
}

static void test_linebuf_split_across_appends(void) {
    /* Simulates a line arriving across three separate recv() calls, which
     * is legal TCP behavior and exactly what a naive line reader gets wrong. */
    linebuf_t lb;
    linebuf_init(&lb);
    linebuf_append(&lb, "EC", 2);
    CHECK(linebuf_take_line(&lb) == NULL, "linebuf: no line yet after partial append 1");
    linebuf_append(&lb, "HO he", 5);
    CHECK(linebuf_take_line(&lb) == NULL, "linebuf: no line yet after partial append 2");
    linebuf_append(&lb, "llo\n", 4);
    char *line = linebuf_take_line(&lb);
    CHECK(line != NULL && strcmp(line, "ECHO hello") == 0, "linebuf: reassembled a line split across 3 recv()s");
    free(line);
    linebuf_free(&lb);
}

static void test_linebuf_multiple_lines_one_append(void) {
    /* The opposite case: several lines arrive in a single recv(). */
    linebuf_t lb;
    linebuf_init(&lb);
    linebuf_append(&lb, "ECHO a\nECHO b\nECHO c\n", 21);
    char *l1 = linebuf_take_line(&lb);
    char *l2 = linebuf_take_line(&lb);
    char *l3 = linebuf_take_line(&lb);
    CHECK(l1 && strcmp(l1, "ECHO a") == 0, "linebuf: first of three lines in one packet");
    CHECK(l2 && strcmp(l2, "ECHO b") == 0, "linebuf: second of three lines in one packet");
    CHECK(l3 && strcmp(l3, "ECHO c") == 0, "linebuf: third of three lines in one packet");
    CHECK(linebuf_take_line(&lb) == NULL, "linebuf: nothing left after three lines consumed");
    free(l1);
    free(l2);
    free(l3);
    linebuf_free(&lb);
}

static void test_linebuf_strips_cr(void) {
    linebuf_t lb;
    linebuf_init(&lb);
    linebuf_append(&lb, "ECHO windows\r\n", 14);
    char *line = linebuf_take_line(&lb);
    CHECK(line != NULL && strcmp(line, "ECHO windows") == 0, "linebuf: trailing \\r stripped for CRLF clients");
    free(line);
    linebuf_free(&lb);
}

static void test_protocol_echo_rev_upper(void) {
    char *out;
    int close_flag, sleep_ms;

    protocol_handle_line("ECHO hello", &out, &close_flag, &sleep_ms);
    CHECK(out && strcmp(out, "hello\n") == 0 && !close_flag && sleep_ms == 0, "protocol: ECHO");
    free(out);

    protocol_handle_line("REV abcd", &out, &close_flag, &sleep_ms);
    CHECK(out && strcmp(out, "dcba\n") == 0, "protocol: REV reverses its argument");
    free(out);

    protocol_handle_line("UPPER shout", &out, &close_flag, &sleep_ms);
    CHECK(out && strcmp(out, "SHOUT\n") == 0, "protocol: UPPER uppercases its argument");
    free(out);
}

static void test_protocol_sleep_defers_to_caller(void) {
    char *out;
    int close_flag, sleep_ms;
    protocol_handle_line("SLEEP 250", &out, &close_flag, &sleep_ms);
    CHECK(out == NULL, "protocol: SLEEP produces no immediate response");
    CHECK(sleep_ms == 250, "protocol: SLEEP reports the requested delay");
    CHECK(!close_flag, "protocol: SLEEP does not close the connection");
}

static void test_protocol_sleep_is_capped(void) {
    char *out;
    int close_flag, sleep_ms;
    protocol_handle_line("SLEEP 999999", &out, &close_flag, &sleep_ms);
    CHECK(sleep_ms == 10000, "protocol: SLEEP is capped at 10000ms so one client can't wedge a slot forever");
}

static void test_protocol_quit_and_unknown(void) {
    char *out;
    int close_flag, sleep_ms;

    protocol_handle_line("QUIT", &out, &close_flag, &sleep_ms);
    CHECK(out && strcmp(out, "BYE\n") == 0 && close_flag, "protocol: QUIT replies BYE and signals close");
    free(out);

    protocol_handle_line("NONSENSE", &out, &close_flag, &sleep_ms);
    CHECK(out && strncmp(out, "ERR", 3) == 0 && !close_flag, "protocol: unrecognized command gets an ERR, not a crash");
    free(out);
}

int main(void) {
    test_linebuf_whole_line_at_once();
    test_linebuf_split_across_appends();
    test_linebuf_multiple_lines_one_append();
    test_linebuf_strips_cr();
    test_protocol_echo_rev_upper();
    test_protocol_sleep_defers_to_caller();
    test_protocol_sleep_is_capped();
    test_protocol_quit_and_unknown();

    if (failures) {
        printf("\n%d check(s) FAILED\n", failures);
        return 1;
    }
    printf("\nAll checks passed\n");
    return 0;
}
