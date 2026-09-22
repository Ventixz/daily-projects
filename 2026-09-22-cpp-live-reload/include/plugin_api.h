#pragma once

extern "C" {

// State the host owns for the lifetime of the process. A plugin never
// allocates this itself -- it only mutates the instance the host hands it
// -- which is what lets a reload swap the *code* without losing the *data*.
struct AppState {
    double y = 100.0;   // height above the ground
    double vy = 0.0;    // vertical velocity
    long ticks = 0;     // frames simulated so far, across every plugin load
};

// Implemented by plugin.cpp (or a fixture under tests/fixtures). Advances
// state by dt seconds of simulated time. This is the function you edit and
// rebuild while the host keeps running.
void plugin_tick(AppState* state, double dt);

// A short tag identifying which build of the plugin is currently loaded,
// so a reload is visible in the host's output without inspecting physics.
const char* plugin_label();

}
