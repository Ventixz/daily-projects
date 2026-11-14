// breakpoint.hpp - a software breakpoint: swap one byte of code for int3 (0xCC).
#pragma once

#include <sys/types.h>

#include <cstdint>

namespace minidbg {

class breakpoint {
public:
    breakpoint() = default;
    breakpoint(pid_t pid, std::uintptr_t addr) : m_pid{pid}, m_addr{addr} {}

    void enable();   // save the original byte, write 0xCC in its place
    void disable();  // put the original byte back

    bool is_enabled() const { return m_enabled; }
    std::uintptr_t get_address() const { return m_addr; }
    std::uint8_t saved_byte() const { return m_saved_data; }

private:
    pid_t m_pid = 0;
    std::uintptr_t m_addr = 0;
    bool m_enabled = false;
    std::uint8_t m_saved_data = 0;
};

}  // namespace minidbg
