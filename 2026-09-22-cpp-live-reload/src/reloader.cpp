#include "reloader.hpp"

#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

long mtime_of(const std::string& path) {
    struct stat st {};
    if (stat(path.c_str(), &st) != 0) return -1;
    return static_cast<long>(st.st_mtime);
}

} // namespace

Reloader::Reloader(std::string so_path) : so_path_(std::move(so_path)) {}

Reloader::~Reloader() {
    if (handle_) dlclose(handle_);
}

bool Reloader::load() {
    last_mtime_ = mtime_of(so_path_);
    if (last_mtime_ < 0) {
        fprintf(stderr, "reloader: %s not found\n", so_path_.c_str());
        return false;
    }
    return load_from_copy();
}

// dlopen() on Linux resolves and refcounts a shared object by its realpath,
// which means dlopen'ing the *same on-disk path* twice -- once, dlclose, then
// again after the build tool has overwritten it -- is not reliably a fresh
// load: whether you actually get the new bytes depends on things outside
// this program's control (link count, mmap page cache behavior). Copying
// each build to a uniquely named file before dlopen sidesteps the whole
// question: every load gets a path the dynamic linker has never seen before,
// so it is unambiguously a new module.
bool Reloader::load_from_copy() {
    fs::path cache_dir = "build/reload_cache";
    std::error_code mkdir_ec;
    fs::create_directories(cache_dir, mkdir_ec);

    fs::path copy_path = cache_dir /
        (fs::path(so_path_).stem().string() + "." + std::to_string(getpid()) +
         "." + std::to_string(generation_) + ".so");

    std::error_code copy_ec;
    fs::copy_file(so_path_, copy_path, fs::copy_options::overwrite_existing, copy_ec);
    if (copy_ec) {
        fprintf(stderr, "reloader: copying %s failed: %s\n", so_path_.c_str(), copy_ec.message().c_str());
        return false;
    }

    void* new_handle = dlopen(copy_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!new_handle) {
        fprintf(stderr, "reloader: dlopen(%s) failed: %s\n", copy_path.c_str(), dlerror());
        return false;
    }

    dlerror(); // clear any prior error before probing symbols
    auto new_tick = reinterpret_cast<void (*)(AppState*, double)>(dlsym(new_handle, "plugin_tick"));
    const char* tick_err = dlerror();
    auto new_label = reinterpret_cast<const char* (*)()>(dlsym(new_handle, "plugin_label"));
    const char* label_err = dlerror();

    if (tick_err || !new_tick || label_err || !new_label) {
        fprintf(stderr, "reloader: %s is missing plugin_tick/plugin_label\n", copy_path.c_str());
        dlclose(new_handle);
        return false;
    }

    // Only drop the old handle once the new one is fully validated, so a
    // broken rebuild never leaves the host without a working plugin.
    if (handle_) dlclose(handle_);
    handle_ = new_handle;
    tick_fn_ = new_tick;
    label_fn_ = new_label;
    generation_++;
    return true;
}

bool Reloader::maybe_reload() {
    long m = mtime_of(so_path_);
    if (m < 0 || m == last_mtime_) return false;

    if (!load_from_copy()) return false;
    last_mtime_ = m;
    return true;
}

void Reloader::tick(AppState* state, double dt) const {
    tick_fn_(state, dt);
}

const char* Reloader::label() const {
    return label_fn_();
}
