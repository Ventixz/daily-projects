// Deliberately dumb fixture plugin used only by tests/unit/reloader_test.cpp
// -- it doesn't simulate anything, it just makes state changes easy to
// assert on.
#include "../../include/plugin_api.h"

extern "C" void plugin_tick(AppState* state, double dt) {
    (void)dt;
    state->y += 1.0;
    state->ticks++;
}

extern "C" const char* plugin_label() { return "fixture-v1"; }
