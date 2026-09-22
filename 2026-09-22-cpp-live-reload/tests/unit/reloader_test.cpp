// No external test framework -- one assert-and-report macro, run directly.
#include "../../include/plugin_api.h"
#include "../../src/reloader.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <thread>

namespace fs = std::filesystem;

static int failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                  \
        } else {                                                          \
            printf("PASS: %s\n", #cond);                                  \
        }                                                                 \
    } while (0)

int main() {
    fs::create_directories("build");
    fs::copy_file("build/fixture_v1.so", "build/plugin_under_test.so",
                   fs::copy_options::overwrite_existing);

    Reloader r("build/plugin_under_test.so");
    CHECK(r.load());
    CHECK(std::strcmp(r.label(), "fixture-v1") == 0);

    AppState s{};
    s.y = 0.0;
    s.vy = 0.0;
    s.ticks = 0;

    r.tick(&s, 1.0);
    CHECK(s.ticks == 1);
    CHECK(s.y == 1.0); // fixture_v1 adds 1.0 to y per tick

    CHECK(r.maybe_reload() == false); // file unchanged: no reload, no-op

    // Simulate a rebuild: swap in fixture_v2's bytes under the same path,
    // with a newer mtime. mtime resolution can be whole seconds on some
    // filesystems, so wait past a second boundary rather than racing it.
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    fs::copy_file("build/fixture_v2.so", "build/plugin_under_test.so",
                   fs::copy_options::overwrite_existing);

    CHECK(r.maybe_reload() == true);
    CHECK(std::strcmp(r.label(), "fixture-v2") == 0);

    r.tick(&s, 1.0);
    CHECK(s.ticks == 2); // ticks kept counting across the reload, not reset
    CHECK(s.y == 3.0);   // 1.0 (v1) + 2.0 (v2) -- same AppState throughout

    if (failures > 0) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("all checks passed\n");
    return 0;
}
