#include <chrono>
#include <cstdio>
#include <cstring>

#include "testing.h"

// Usage: <binary> [substring]   runs the tests whose name contains substring.
int main(int argc, char **argv) {
    const char *filter = argc > 1 ? argv[1] : "";
    int passed = 0, failed = 0;
    for (const testing::Case &c : testing::registry()) {
        if (!std::strstr(c.name, filter)) continue;
        auto t0 = std::chrono::steady_clock::now();
        try {
            c.fn();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - t0)
                          .count();
            std::printf("ok    %-52s %5lld ms\n", c.name, static_cast<long long>(ms));
            passed++;
        } catch (const testing::Failure &f) {
            std::printf("FAIL  %s\n  %s\n", c.name, f.message.c_str());
            failed++;
        } catch (const std::exception &e) {
            std::printf("FAIL  %s\n  exception: %s\n", c.name, e.what());
            failed++;
        }
        std::fflush(stdout);
    }
    std::printf("\n%d passed, %d failed\n", passed, failed);
    return failed == 0 && passed > 0 ? 0 : 1;
}
