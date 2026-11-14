// debugger.hpp - launch a program under ptrace and control it.
#pragma once

#include <sys/types.h>

#include <cstdint>
#include <iosfwd>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "breakpoint.hpp"
#include "elf_symbols.hpp"
#include "registers.hpp"

namespace minidbg {

// Why the tracee stopped (or stopped existing) after continue / stepi.
struct stop_event {
    enum class kind { breakpoint, step, signal, exited, killed } what;
    int code = 0;              // exit status for exited, signal number for signal / killed
    std::uintptr_t pc = 0;     // where it stopped (0 once the process is gone)
};

class debugger {
public:
    // Forks, disables ASLR in the child, PTRACE_TRACEMEs and execs `program`.
    // Returns with the child stopped on its first instruction (the exec SIGTRAP).
    explicit debugger(std::string program, std::vector<std::string> args = {});
    ~debugger();  // kills the tracee if it is still alive

    debugger(const debugger&) = delete;
    debugger& operator=(const debugger&) = delete;

    pid_t pid() const { return m_pid; }
    bool alive() const { return m_alive; }
    int exit_code() const { return m_exit_code; }

    // ---- execution
    stop_event continue_execution();
    stop_event single_step();

    // ---- breakpoints
    void set_breakpoint_at_address(std::uintptr_t addr);
    bool remove_breakpoint(std::uintptr_t addr);
    const std::map<std::uintptr_t, breakpoint>& breakpoints() const { return m_breakpoints; }

    // ---- registers and memory
    std::uint64_t get_pc() const;
    void set_pc(std::uint64_t pc);
    std::uint64_t read_memory(std::uintptr_t addr) const;          // one 8-byte word, raw
    void write_memory(std::uintptr_t addr, std::uint64_t value);   // one 8-byte word
    // Bytes as the program sees them: our int3 patches are hidden.
    std::vector<std::uint8_t> read_bytes(std::uintptr_t addr, std::size_t n) const;

    // ---- addresses
    std::uint64_t load_address() const { return m_load_address; }  // 0 for non-PIE
    const elf_symbols& symbols() const { return m_symbols; }
    // "0x401136" (absolute), "+0x1139" (offset from load base) or a symbol name.
    std::optional<std::uintptr_t> resolve(const std::string& location) const;
    // "add+4" style description of a runtime address, or "" if no symbol covers it.
    std::string describe(std::uintptr_t addr) const;

    // ---- command line
    // Runs one command, writes its output to `out`. Returns false for "quit".
    bool handle_command(const std::string& line, std::ostream& out);
    void run(std::istream& in, std::ostream& out);  // the read-eval-print loop

private:
    void step_over_breakpoint();
    stop_event wait_for_signal();
    void require_alive() const;
    void read_load_address();

    std::string m_program;
    pid_t m_pid = -1;
    bool m_alive = false;
    int m_exit_code = -1;
    int m_pending_signal = 0;  // a non-trap signal to hand back to the program on the next continue
    std::uint64_t m_load_address = 0;
    elf_symbols m_symbols;
    std::map<std::uintptr_t, breakpoint> m_breakpoints;
};

}  // namespace minidbg
