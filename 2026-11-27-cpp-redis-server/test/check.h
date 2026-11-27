// A very small test harness: TEST(name) registers a function, CHECK records a
// failure with file:line and keeps going, main() runs everything.
#pragma once
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

struct TestCase {
    const char *name;
    std::function<void()> fn;
};

inline std::vector<TestCase> &test_registry() {
    static std::vector<TestCase> tests;
    return tests;
}
inline int &test_failures() {
    static int n = 0;
    return n;
}

struct TestRegistrar {
    TestRegistrar(const char *name, std::function<void()> fn) {
        test_registry().push_back({name, std::move(fn)});
    }
};

#define TEST_CAT2(a, b) a##b
#define TEST_CAT(a, b) TEST_CAT2(a, b)
#define TEST(name)                                                      \
    static void name();                                                 \
    static TestRegistrar TEST_CAT(reg_, name)(#name, name);             \
    static void name()

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            fprintf(stderr, "    %s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
            test_failures()++;                                                   \
        }                                                                        \
    } while (0)

#define CHECK_EQ(a, b)                                                            \
    do {                                                                          \
        auto va_ = (a);                                                           \
        auto vb_ = (b);                                                           \
        if (!(va_ == vb_)) {                                                      \
            fprintf(stderr, "    %s:%d: CHECK_EQ(%s, %s) failed\n", __FILE__, __LINE__, #a, #b); \
            test_failures()++;                                                    \
        }                                                                         \
    } while (0)

inline int run_all_tests() {
    int failed_tests = 0;
    for (auto &t : test_registry()) {
        int before = test_failures();
        t.fn();
        bool ok = test_failures() == before;
        printf("%s %s\n", ok ? "PASS" : "FAIL", t.name);
        fflush(stdout);
        failed_tests += !ok;
    }
    printf("\n%zu tests, %d failed\n", test_registry().size(), failed_tests);
    return failed_tests ? 1 : 0;
}
