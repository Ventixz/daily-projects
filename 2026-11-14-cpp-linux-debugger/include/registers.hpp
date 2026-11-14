// registers.hpp - x86-64 register table on top of PTRACE_GETREGS / PTRACE_SETREGS.
#pragma once

#include <sys/types.h>
#include <sys/user.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace minidbg {

enum class reg {
    rax, rbx, rcx, rdx,
    rdi, rsi, rbp, rsp,
    r8,  r9,  r10, r11,
    r12, r13, r14, r15,
    rip, rflags, cs,
    orig_rax, fs_base,
    gs_base,
    fs, gs, ss, ds, es
};

constexpr std::size_t n_registers = 27;

struct reg_descriptor {
    reg r;
    int dwarf_r;       // DWARF register number, -1 if none (used by later parts of the series)
    const char* name;
};

// Order matches the field order of `struct user_regs_struct` in <sys/user.h>.
// That lets us treat the struct as an array of uint64_t and index it by
// position in this table. A static_assert in registers.cpp checks the size.
extern const std::array<reg_descriptor, n_registers> g_register_descriptors;

uint64_t get_register_value(pid_t pid, reg r);
void set_register_value(pid_t pid, reg r, uint64_t value);

std::string get_register_name(reg r);
std::optional<reg> get_register_from_name(const std::string& name);
std::optional<reg> get_register_from_dwarf(int dwarf_r);

}  // namespace minidbg
