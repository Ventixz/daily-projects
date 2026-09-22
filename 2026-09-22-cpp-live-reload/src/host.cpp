// The long-running process. Loads a plugin .so and simulates a bouncing
// ball, one tick at a time, forever checking whether the plugin file on
// disk has been rebuilt. AppState lives here, not in the plugin, so a
// reload can never lose it.
#include "reloader.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

int main(int argc, char** argv) {
    if (argc < 5) {
        fprintf(stderr, "usage: %s <plugin.so> <ticks> <dt-seconds> <sleep-ms>\n", argv[0]);
        return 1;
    }
    std::string plugin_path = argv[1];
    int ticks = std::atoi(argv[2]);
    double dt = std::atof(argv[3]);
    int sleep_ms = std::atoi(argv[4]);

    Reloader reloader(plugin_path);
    if (!reloader.load()) {
        fprintf(stderr, "host: initial load of %s failed\n", plugin_path.c_str());
        return 1;
    }

    AppState state{};
    printf("[host] loaded %s\n", reloader.label());
    fflush(stdout);

    for (int i = 0; i < ticks; ++i) {
        if (reloader.maybe_reload()) {
            printf("[host] reloaded plugin -> %s (ticks=%ld carried over)\n", reloader.label(), state.ticks);
            fflush(stdout);
        }

        reloader.tick(&state, dt);
        printf("tick=%ld label=%s y=%.3f vy=%.3f\n", state.ticks, reloader.label(), state.y, state.vy);
        fflush(stdout);

        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    }

    return 0;
}
