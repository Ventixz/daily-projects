/* A minimal test harness: CHECK records a failure and keeps going. */
#ifndef TEST_H
#define TEST_H

#include <stdio.h>
#include <stdlib.h>

static int g_failures = 0;
static int g_tests = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++;                                                    \
        }                                                                    \
    } while (0)

#define RUN(fn)                                   \
    do {                                          \
        int before = g_failures;                  \
        fn();                                     \
        g_tests++;                                \
        printf("%s %s\n", g_failures == before ? "ok  " : "FAIL", #fn); \
    } while (0)

#define DONE()                                                        \
    do {                                                              \
        printf("%d tests, %d failed checks\n", g_tests, g_failures); \
        return g_failures ? 1 : 0;                                    \
    } while (0)

#endif
