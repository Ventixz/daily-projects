# Live Code-Reloader for C++ (C++)

**Source:** [Build a Live Code-reloader Library for C++](http://howistart.org/posts/cpp/1/index.html),
from the C++ section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).

Picked and built end-to-end in one sitting rather than scaffolded-then-attempted, so this folder
contains the finished implementation directly at the project root (no separate `reference/`).

## What it is

A host process (`src/host.cpp`) simulates a bouncing ball forever, one tick at a time, by calling
into `plugin/plugin.cpp` -- built as a separate `.so` -- for the physics. `src/reloader.{hpp,cpp}`
is the reusable part: a `Reloader` class that polls the plugin file's mtime and, when it changes,
`dlopen`s the new build and swaps in its symbols, live, while the host keeps running. The ball's
position and velocity (`AppState`, in `include/plugin_api.h`) are owned by the host, not the
plugin, which is what makes a reload a code swap instead of a restart.

- `include/plugin_api.h` -- the C ABI a plugin must implement: `plugin_tick(AppState*, double)`
  and `plugin_label()`. `AppState` lives here too, since both sides need its layout.
- `src/reloader.{hpp,cpp}` -- `dlopen`/`dlsym`/`dlclose` plus the mtime check, isolated so it's
  unit-testable without a real running host.
- `src/host.cpp` -- the "game loop": construct a `Reloader`, own an `AppState`, tick both every
  frame, print what happened.
- `plugin/plugin.cpp` -- the file you'd actually edit day to day. Two constants (`gravity`,
  `restitution`) and nothing else.
- `tests/fixtures/fixture_v{1,2}.cpp` -- two trivial, deterministic plugins used only by the unit
  test, standing in for "before you edited it" / "after you edited it".
- `tests/unit/reloader_test.cpp` -- loads fixture_v1, ticks it, swaps in fixture_v2 on disk,
  reloads, ticks again, and asserts the `AppState` from the first tick is still there for the
  second. No process spawning, no host binary.
- `e2e/smoke.sh` -- runs the *real* host and plugin, rebuilds the plugin mid-run from a scratch
  copy (never touching the tracked `plugin/plugin.cpp`), and checks the host's own log: the label
  it prints flips from `gravity-v1` to `gravity-v2`, and the `tick=` counter is contiguous across
  that flip -- proof the reload didn't restart the simulation.

## Run it

```powershell
# Windows 11 / PowerShell (needs a POSIX-ish dlopen -- build under WSL2 or MSYS2/MinGW; plain
# MSVC has no dlopen/dlsym, so this project specifically targets a Linux/POSIX toolchain)
wsl --install
wsl
sudo apt update && sudo apt install -y build-essential
```

```bash
# Linux/WSL2/macOS
make build          # builds build/host and build/plugin.so
make run            # runs the host for ~20s, printing every tick
```

To see a reload happen by hand: while `make run` is going in one terminal, edit the
`restitution` constant in `plugin/plugin.cpp` in another, then in a third terminal run
`make build/plugin.so` (or just `make build`). The running host's next tick will print
`[host] reloaded plugin -> gravity-v1 (ticks=N carried over)` and the ball's bounce height
will change immediately, with `ticks` continuing from wherever it was.

```bash
make test    # unit tests: reloader logic against two fixture plugins, no host process
make e2e     # builds the real host+plugin, reloads it mid-run, checks the log
```

## What it actually teaches

- **`dlopen`'s path-based caching is a trap for hot-reload.** The naive approach --
  `dlclose(handle); dlopen(same_path)` after a rebuild -- is not reliably a fresh load, because
  the dynamic linker tracks loaded objects by resolved path/refcount, and what you get back after
  a `dlclose` that raced a file overwrite is unspecified in practice. `Reloader::load_from_copy()`
  copies every build to a uniquely named file (`build/reload_cache/plugin.<pid>.<gen>.so`) before
  `dlopen`-ing it, so every load is unambiguously new to the linker -- there's no cache to race.
- **State has to live outside the thing you're reloading.** `AppState` is a plain struct owned by
  `host.cpp` and passed by pointer into `plugin_tick`. The plugin never allocates or owns it. If
  the ball's position lived inside `plugin.cpp` (say, as a static), every reload would reset it to
  zero -- there'd be nothing to "carry over". Pushing state up to the host and keeping the plugin
  a pure function of `(state, dt) -> state` is the entire trick.
- **Validate before you swap.** `load_from_copy()` only calls `dlclose()` on the *old* handle after
  the *new* one has `dlopen`ed successfully and both symbols have resolved. A rebuild with a typo
  that fails to link, or that's missing `plugin_label`, leaves the previous working plugin active
  instead of crashing the host or leaving it with null function pointers.
- **mtime polling instead of `inotify`.** A file-watching API would react faster and use less CPU,
  but it's one more moving part and one more failure mode (watch descriptors, `EAGAIN`, platform
  differences) for a problem `stat()` already solves at the cost of one syscall per tick. Noted
  below as a stretch goal, not built, because the reload mechanism itself -- not how you detect
  "something changed" -- is what this project is about.

## What I'd add next (stretch goals I skipped for scope)

- Swap `stat()`-per-tick for `inotify` (Linux) so a rebuild is picked up the instant it lands
  instead of on the next tick.
- A `plugin_on_unload(AppState*)` hook, called on the *old* plugin right before it's dropped, for
  plugins that need to flush something plugin-local (this project never needs one, since all real
  state already lives in the host).
- Versioned `AppState` with a schema check, so a reload that changes the struct's layout fails
  loudly instead of reading garbage through stale field offsets.

## License

Licensed under the MIT License; see the LICENSE file at the repository root.
Tutorial: ["Build a Live Code-reloader Library for C++"](http://howistart.org/posts/cpp/1/index.html).
