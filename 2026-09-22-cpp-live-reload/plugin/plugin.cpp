// The one file in this project meant to be edited while the host is
// running: change a constant below, `make build/plugin.so`, and the
// running host picks it up on its next reload check -- no restart, and
// the ball's height/velocity (owned by the host, not this file) keeps
// going from wherever it was.
#include "../include/plugin_api.h"

extern "C" void plugin_tick(AppState* state, double dt) {
    const double gravity = 9.8;      // m/s^2
    const double restitution = 0.6;  // fraction of speed kept per bounce

    state->vy -= gravity * dt;
    state->y += state->vy * dt;
    if (state->y <= 0.0) {
        state->y = 0.0;
        state->vy = -state->vy * restitution;
    }
    state->ticks++;
}

extern "C" const char* plugin_label() { return "gravity-v1"; }
