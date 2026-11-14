// breakpoint.cpp - int3 insertion with PTRACE_PEEKDATA / PTRACE_POKEDATA.
#include "breakpoint.hpp"

#include <sys/ptrace.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

namespace minidbg {

namespace {

// PEEKDATA returns the word itself, so -1 is a valid value. The only way to
// tell a failure apart is to clear errno first and check it afterwards.
long peek(pid_t pid, std::uintptr_t addr) {
    errno = 0;
    long word = ptrace(PTRACE_PEEKDATA, pid, addr, nullptr);
    if (errno != 0)
        throw std::runtime_error("cannot read tracee memory at " + std::to_string(addr) + ": " +
                                 std::strerror(errno));
    return word;
}

void poke(pid_t pid, std::uintptr_t addr, long word) {
    if (ptrace(PTRACE_POKEDATA, pid, addr, word) == -1)
        throw std::runtime_error(std::string("cannot write tracee memory: ") + std::strerror(errno));
}

}  // namespace

void breakpoint::enable() {
    if (m_enabled) return;
    long data = peek(m_pid, m_addr);
    m_saved_data = static_cast<std::uint8_t>(data & 0xff);  // x86 is little-endian: low byte = byte at m_addr
    long int3 = 0xcc;
    long patched = (data & ~0xffL) | int3;
    poke(m_pid, m_addr, patched);
    m_enabled = true;
}

void breakpoint::disable() {
    if (!m_enabled) return;
    long data = peek(m_pid, m_addr);
    long restored = (data & ~0xffL) | m_saved_data;
    poke(m_pid, m_addr, restored);
    m_enabled = false;
}

}  // namespace minidbg
