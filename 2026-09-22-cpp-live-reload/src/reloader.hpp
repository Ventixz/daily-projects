#pragma once

#include "../include/plugin_api.h"

#include <string>

// Loads a plugin shared object built at `so_path`, watches it for rebuilds,
// and swaps the active plugin_tick/plugin_label symbols in place when it
// changes -- without the host process restarting or losing any AppState.
class Reloader {
public:
    explicit Reloader(std::string so_path);
    ~Reloader();

    Reloader(const Reloader&) = delete;
    Reloader& operator=(const Reloader&) = delete;

    // Loads so_path for the first time. Must succeed before tick()/label().
    bool load();

    // Compares so_path's mtime against the last (re)load. If it changed,
    // loads the new build and swaps in its symbols. Returns true iff it
    // reloaded; false (including "file missing" or "load failed") means
    // the previously loaded plugin is still active.
    bool maybe_reload();

    void tick(AppState* state, double dt) const;
    const char* label() const;

private:
    bool load_from_copy();

    std::string so_path_;
    void* handle_ = nullptr;
    void (*tick_fn_)(AppState*, double) = nullptr;
    const char* (*label_fn_)() = nullptr;
    long last_mtime_ = -1;
    int generation_ = 0;
};
