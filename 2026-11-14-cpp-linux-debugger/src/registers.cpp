// registers.cpp - read and write the tracee's registers by name.
#include "registers.hpp"

#include <sys/ptrace.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <stdexcept>

namespace minidbg {

const std::array<reg_descriptor, n_registers> g_register_descriptors{{
    {reg::r15, 15, "r15"},
    {reg::r14, 14, "r14"},
    {reg::r13, 13, "r13"},
    {reg::r12, 12, "r12"},
    {reg::rbp, 6, "rbp"},
    {reg::rbx, 3, "rbx"},
    {reg::r11, 11, "r11"},
    {reg::r10, 10, "r10"},
    {reg::r9, 9, "r9"},
    {reg::r8, 8, "r8"},
    {reg::rax, 0, "rax"},
    {reg::rcx, 2, "rcx"},
    {reg::rdx, 1, "rdx"},
    {reg::rsi, 4, "rsi"},
    {reg::rdi, 5, "rdi"},
    {reg::orig_rax, -1, "orig_rax"},
    {reg::rip, -1, "rip"},
    {reg::cs, 51, "cs"},
    {reg::rflags, 49, "eflags"},
    {reg::rsp, 7, "rsp"},
    {reg::ss, 52, "ss"},
    {reg::fs_base, 58, "fs_base"},
    {reg::gs_base, 59, "gs_base"},
    {reg::ds, 53, "ds"},
    {reg::es, 50, "es"},
    {reg::fs, 54, "fs"},
    {reg::gs, 55, "gs"},
}};

static_assert(sizeof(user_regs_struct) == n_registers * sizeof(uint64_t),
              "register table must cover every field of user_regs_struct");

namespace {

std::size_t index_of(reg r) {
    auto it = std::find_if(g_register_descriptors.begin(), g_register_descriptors.end(),
                           [r](const reg_descriptor& d) { return d.r == r; });
    if (it == g_register_descriptors.end()) throw std::out_of_range("unknown register");
    return static_cast<std::size_t>(it - g_register_descriptors.begin());
}

user_regs_struct read_all(pid_t pid) {
    user_regs_struct regs{};
    if (ptrace(PTRACE_GETREGS, pid, nullptr, &regs) == -1)
        throw std::runtime_error(std::string("PTRACE_GETREGS: ") + std::strerror(errno));
    return regs;
}

}  // namespace

uint64_t get_register_value(pid_t pid, reg r) {
    user_regs_struct regs = read_all(pid);
    uint64_t words[n_registers];
    std::memcpy(words, &regs, sizeof regs);  // memcpy instead of a cast keeps aliasing rules happy
    return words[index_of(r)];
}

void set_register_value(pid_t pid, reg r, uint64_t value) {
    user_regs_struct regs = read_all(pid);
    uint64_t words[n_registers];
    std::memcpy(words, &regs, sizeof regs);
    words[index_of(r)] = value;
    std::memcpy(&regs, words, sizeof regs);
    if (ptrace(PTRACE_SETREGS, pid, nullptr, &regs) == -1)
        throw std::runtime_error(std::string("PTRACE_SETREGS: ") + std::strerror(errno));
}

std::string get_register_name(reg r) { return g_register_descriptors[index_of(r)].name; }

std::optional<reg> get_register_from_name(const std::string& name) {
    for (const auto& d : g_register_descriptors)
        if (name == d.name) return d.r;
    if (name == "rflags") return reg::rflags;  // accept both spellings
    return std::nullopt;
}

std::optional<reg> get_register_from_dwarf(int dwarf_r) {
    if (dwarf_r < 0) return std::nullopt;
    for (const auto& d : g_register_descriptors)
        if (d.dwarf_r == dwarf_r) return d.r;
    return std::nullopt;
}

}  // namespace minidbg
