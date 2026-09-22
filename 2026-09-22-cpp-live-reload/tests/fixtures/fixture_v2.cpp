// The "rebuilt" counterpart to fixture_v1.cpp -- same shape, a different
// increment, so a test can tell which one is actually loaded.
#include "../../include/plugin_api.h"

extern "C" void plugin_tick(AppState* state, double dt) {
    (void)dt;
    state->y += 2.0;
    state->ticks++;
}

extern "C" const char* plugin_label() { return "fixture-v2"; }
