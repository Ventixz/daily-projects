// debugger.cpp - the ptrace control loop and the command interpreter.
#include "debugger.hpp"

#include <signal.h>
#include <sys/personality.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace minidbg {

namespace {

std::string errstr(const char* what) { return std::string(what) + ": " + std::strerror(errno); }

std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream in(s);
    for (std::string w; in >> w;) out.push_back(w);
    return out;
}

// "c" matches "continue", like gdb: any non-empty prefix of the full name.
bool is_prefix(const std::string& s, const std::string& of) {
    return !s.empty() && s.size() <= of.size() && of.compare(0, s.size(), s) == 0;
}

std::string hex(std::uint64_t v, int width = 16) {
    std::ostringstream o;
    o << "0x" << std::hex << std::setfill('0') << std::setw(width) << v;
    return o.str();
}

std::optional<std::uint64_t> parse_number(const std::string& s) {
    if (s.empty()) return std::nullopt;
    errno = 0;
    char* end = nullptr;
    unsigned long long v = std::strtoull(s.c_str(), &end, 0);  // base 0: 0x.. hex, otherwise decimal
    if (errno != 0 || *end != '\0' || s[0] == '-') return std::nullopt;
    return v;
}

}  // namespace

// ---------------------------------------------------------------- lifecycle

debugger::debugger(std::string program, std::vector<std::string> args)
    : m_program(std::move(program)), m_symbols(m_program) {
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(m_program.c_str()));
    for (auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    pid_t pid = fork();
    if (pid == -1) throw std::runtime_error(errstr("fork"));
    if (pid == 0) {
        // Child. Fixed addresses make sessions repeatable (PIE loads at 0x555555554000).
        personality(ADDR_NO_RANDOMIZE);
        if (ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) == -1) _exit(126);
        execv(m_program.c_str(), argv.data());
        _exit(127);  // only reached if exec failed
    }

    m_pid = pid;
    int status = 0;
    waitpid(m_pid, &status, 0);  // a successful exec under TRACEME stops the child with SIGTRAP
    if (!WIFSTOPPED(status)) throw std::runtime_error("failed to start " + m_program);
    m_alive = true;
    // If the debugger dies, take the tracee with it instead of leaving it stopped forever.
    ptrace(PTRACE_SETOPTIONS, m_pid, nullptr, PTRACE_O_EXITKILL);
    read_load_address();
}

debugger::~debugger() {
    if (m_alive) {
        kill(m_pid, SIGKILL);
        int status;
        waitpid(m_pid, &status, 0);
    }
}

void debugger::read_load_address() {
    if (!m_symbols.is_pie()) { m_load_address = 0; return; }
    char real[PATH_MAX];
    std::string want = realpath(m_program.c_str(), real) ? real : m_program;
    std::ifstream maps("/proc/" + std::to_string(m_pid) + "/maps");
    // Each line: start-end perms offset dev inode path. The mapping of our
    // binary at file offset 0 starts at the load base.
    for (std::string line; std::getline(maps, line);) {
        std::istringstream in(line);
        std::string range, perms, offset, dev, inode, path;
        in >> range >> perms >> offset >> dev >> inode >> path;
        if (path == want && std::stoull(offset, nullptr, 16) == 0) {
            m_load_address = std::stoull(range.substr(0, range.find('-')), nullptr, 16);
            return;
        }
    }
    throw std::runtime_error("could not find load address of " + want);
}

void debugger::require_alive() const {
    if (!m_alive) throw std::runtime_error("the program is not running");
}

// ---------------------------------------------------------------- execution

stop_event debugger::wait_for_signal() {
    int status = 0;
    if (waitpid(m_pid, &status, 0) == -1) throw std::runtime_error(errstr("waitpid"));

    if (WIFEXITED(status)) {
        m_alive = false;
        m_exit_code = WEXITSTATUS(status);
        return {stop_event::kind::exited, m_exit_code, 0};
    }
    if (WIFSIGNALED(status)) {
        m_alive = false;
        return {stop_event::kind::killed, WTERMSIG(status), 0};
    }

    siginfo_t info{};
    ptrace(PTRACE_GETSIGINFO, m_pid, nullptr, &info);
    int sig = WSTOPSIG(status);

    if (sig == SIGTRAP) {
        std::uint64_t pc = get_pc();
        bool from_int3 = info.si_code == SI_KERNEL || info.si_code == TRAP_BRKPT;
        if (from_int3) {
            // The CPU reports the trap after executing the 1-byte int3, so
            // rip is one past the breakpoint. Wind it back so the original
            // instruction runs when we resume.
            auto it = m_breakpoints.find(pc - 1);
            if (it != m_breakpoints.end() && it->second.is_enabled()) {
                set_pc(pc - 1);
                return {stop_event::kind::breakpoint, SIGTRAP, pc - 1};
            }
        }
        return {stop_event::kind::step, SIGTRAP, pc};  // single-step (TRAP_TRACE) or other trap
    }

    // Anything else (SIGSEGV, SIGINT, ...) is the program's business: report it and
    // deliver it on the next continue, otherwise it would be silently swallowed.
    m_pending_signal = sig;
    return {stop_event::kind::signal, sig, get_pc()};
}

void debugger::step_over_breakpoint() {
    std::uint64_t pc = get_pc();
    auto it = m_breakpoints.find(pc);
    if (it == m_breakpoints.end() || !it->second.is_enabled()) return;

    // Put the real instruction back, execute exactly that one instruction,
    // then re-arm the breakpoint so it fires next time round.
    it->second.disable();
    if (ptrace(PTRACE_SINGLESTEP, m_pid, nullptr, nullptr) == -1) throw std::runtime_error(errstr("singlestep"));
    int status = 0;
    waitpid(m_pid, &status, 0);
    if (WIFEXITED(status) || WIFSIGNALED(status)) {
        m_alive = false;
        m_exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        return;
    }
    it->second.enable();
}

stop_event debugger::continue_execution() {
    require_alive();
    step_over_breakpoint();
    if (!m_alive) return {stop_event::kind::exited, m_exit_code, 0};
    int sig = m_pending_signal;
    m_pending_signal = 0;
    if (ptrace(PTRACE_CONT, m_pid, nullptr, sig) == -1) throw std::runtime_error(errstr("PTRACE_CONT"));
    return wait_for_signal();
}

stop_event debugger::single_step() {
    require_alive();
    auto it = m_breakpoints.find(get_pc());
    if (it != m_breakpoints.end() && it->second.is_enabled()) {
        step_over_breakpoint();  // that already executes exactly one instruction
        if (!m_alive) return {stop_event::kind::exited, m_exit_code, 0};
        return {stop_event::kind::step, SIGTRAP, get_pc()};
    }
    if (ptrace(PTRACE_SINGLESTEP, m_pid, nullptr, nullptr) == -1) throw std::runtime_error(errstr("singlestep"));
    return wait_for_signal();
}

// ---------------------------------------------------------------- breakpoints

void debugger::set_breakpoint_at_address(std::uintptr_t addr) {
    require_alive();
    if (m_breakpoints.count(addr)) return;
    breakpoint bp{m_pid, addr};
    bp.enable();
    m_breakpoints[addr] = bp;
}

bool debugger::remove_breakpoint(std::uintptr_t addr) {
    auto it = m_breakpoints.find(addr);
    if (it == m_breakpoints.end()) return false;
    if (m_alive) it->second.disable();
    m_breakpoints.erase(it);
    return true;
}

// ---------------------------------------------------------------- registers / memory

std::uint64_t debugger::get_pc() const { return get_register_value(m_pid, reg::rip); }
void debugger::set_pc(std::uint64_t pc) { set_register_value(m_pid, reg::rip, pc); }

std::uint64_t debugger::read_memory(std::uintptr_t addr) const {
    require_alive();
    errno = 0;
    long word = ptrace(PTRACE_PEEKDATA, m_pid, addr, nullptr);
    if (errno != 0) throw std::runtime_error("cannot read memory at " + hex(addr) + ": " + std::strerror(errno));
    return static_cast<std::uint64_t>(word);
}

void debugger::write_memory(std::uintptr_t addr, std::uint64_t value) {
    require_alive();
    if (ptrace(PTRACE_POKEDATA, m_pid, addr, value) == -1)
        throw std::runtime_error("cannot write memory at " + hex(addr) + ": " + std::strerror(errno));
}

std::vector<std::uint8_t> debugger::read_bytes(std::uintptr_t addr, std::size_t n) const {
    std::vector<std::uint8_t> out;
    out.reserve(n);
    std::uintptr_t start = addr & ~std::uintptr_t{7};  // PEEKDATA works in whole words
    for (std::uintptr_t w = start; w < addr + n; w += 8) {
        std::uint64_t word = read_memory(w);
        for (int i = 0; i < 8; ++i) {
            std::uintptr_t a = w + i;
            if (a >= addr && a < addr + n) out.push_back(static_cast<std::uint8_t>(word >> (8 * i)));
        }
    }
    for (const auto& [bp_addr, bp] : m_breakpoints)  // hide our 0xCC patches
        if (bp.is_enabled() && bp_addr >= addr && bp_addr < addr + n) out[bp_addr - addr] = bp.saved_byte();
    return out;
}

// ---------------------------------------------------------------- addresses

std::optional<std::uintptr_t> debugger::resolve(const std::string& location) const {
    if (location.empty()) return std::nullopt;
    if (location[0] == '+') {
        auto off = parse_number(location.substr(1));
        if (!off) return std::nullopt;
        return m_load_address + *off;
    }
    if (auto n = parse_number(location)) return *n;
    if (auto sym = m_symbols.find(location)) return m_load_address + sym->value;
    return std::nullopt;
}

std::string debugger::describe(std::uintptr_t addr) const {
    if (addr < m_load_address) return "";
    auto fn = m_symbols.function_at(addr - m_load_address);
    if (!fn) return "";
    std::uint64_t off = addr - m_load_address - fn->value;
    return off ? fn->name + "+" + std::to_string(off) : fn->name;
}

// ---------------------------------------------------------------- commands

namespace {

void report(const debugger& dbg, const stop_event& ev, std::ostream& out) {
    switch (ev.what) {
        case stop_event::kind::breakpoint:
            out << "Breakpoint hit at " << hex(ev.pc);
            break;
        case stop_event::kind::step:
            out << "Stopped at " << hex(ev.pc);
            break;
        case stop_event::kind::signal:
            out << "Program received signal " << ev.code << " (" << strsignal(ev.code) << ") at " << hex(ev.pc);
            break;
        case stop_event::kind::exited:
            out << "Program exited with code " << ev.code << "\n";
            return;
        case stop_event::kind::killed:
            out << "Program terminated by signal " << ev.code << " (" << strsignal(ev.code) << ")\n";
            return;
    }
    std::string where = dbg.describe(ev.pc);
    if (!where.empty()) out << " <" << where << ">";
    out << "\n";
}

const char* kHelp =
    "Commands (any unambiguous prefix works, e.g. 'c', 'b', 'reg'):\n"
    "  continue                      run until a breakpoint, signal or exit\n"
    "  stepi                         execute one machine instruction\n"
    "  break <loc>                   set a breakpoint\n"
    "  delete <loc>                  remove a breakpoint\n"
    "  info                          list breakpoints\n"
    "  register dump                 print every register\n"
    "  register read <reg>           print one register\n"
    "  register write <reg> <value>  set one register\n"
    "  memory read <loc>             print the 8-byte word at <loc>\n"
    "  memory write <loc> <value>    store an 8-byte word at <loc>\n"
    "  x <loc> [count]               hex dump <count> bytes (default 16)\n"
    "  where                         print the current instruction address\n"
    "  symbols [filter]              list functions and globals\n"
    "  quit\n"
    "<loc> is a symbol (main), an absolute address (0x401136) or an offset\n"
    "from the load base (+0x1139). Values are decimal or 0x-prefixed hex.\n";

}  // namespace

bool debugger::handle_command(const std::string& line, std::ostream& out) {
    auto args = split(line);
    if (args.empty()) return true;
    const std::string& cmd = args[0];

    auto need_loc = [&](std::size_t i) -> std::uintptr_t {
        if (args.size() <= i) throw std::runtime_error("missing location");
        auto a = resolve(args[i]);
        if (!a) throw std::runtime_error("unknown location '" + args[i] + "'");
        return *a;
    };
    auto need_value = [&](std::size_t i) -> std::uint64_t {
        if (args.size() <= i) throw std::runtime_error("missing value");
        auto v = parse_number(args[i]);
        if (!v) throw std::runtime_error("bad value '" + args[i] + "'");
        return *v;
    };
    auto need_reg = [&](std::size_t i) -> reg {
        if (args.size() <= i) throw std::runtime_error("missing register name");
        auto r = get_register_from_name(args[i]);
        if (!r) throw std::runtime_error("unknown register '" + args[i] + "'");
        return *r;
    };

    try {
        if (cmd == "q" || is_prefix(cmd, "quit")) return false;
        if (cmd == "h" || is_prefix(cmd, "help")) {
            out << kHelp;
        } else if (is_prefix(cmd, "continue")) {
            report(*this, continue_execution(), out);
        } else if (cmd == "si" || is_prefix(cmd, "stepi")) {
            report(*this, single_step(), out);
        } else if (is_prefix(cmd, "break")) {
            auto addr = need_loc(1);
            set_breakpoint_at_address(addr);
            out << "Breakpoint set at " << hex(addr);
            if (auto d = describe(addr); !d.empty()) out << " <" << d << ">";
            out << "\n";
        } else if (is_prefix(cmd, "delete")) {
            auto addr = need_loc(1);
            out << (remove_breakpoint(addr) ? "Deleted breakpoint at " : "No breakpoint at ") << hex(addr) << "\n";
        } else if (is_prefix(cmd, "info")) {
            if (m_breakpoints.empty()) out << "No breakpoints.\n";
            for (const auto& [addr, bp] : m_breakpoints) {
                out << hex(addr) << (bp.is_enabled() ? "  enabled" : "  disabled");
                if (auto d = describe(addr); !d.empty()) out << "  <" << d << ">";
                out << "\n";
            }
        } else if (is_prefix(cmd, "register")) {
            require_alive();
            if (args.size() < 2) throw std::runtime_error("usage: register dump|read|write");
            if (is_prefix(args[1], "dump")) {
                for (const auto& d : g_register_descriptors)
                    out << std::left << std::setw(9) << d.name << hex(get_register_value(m_pid, d.r)) << "\n";
            } else if (is_prefix(args[1], "read")) {
                out << hex(get_register_value(m_pid, need_reg(2))) << "\n";
            } else if (is_prefix(args[1], "write")) {
                set_register_value(m_pid, need_reg(2), need_value(3));
            } else {
                throw std::runtime_error("usage: register dump|read|write");
            }
        } else if (is_prefix(cmd, "memory")) {
            if (args.size() < 2) throw std::runtime_error("usage: memory read|write");
            if (is_prefix(args[1], "read")) {
                out << hex(read_memory(need_loc(2))) << "\n";
            } else if (is_prefix(args[1], "write")) {
                write_memory(need_loc(2), need_value(3));
            } else {
                throw std::runtime_error("usage: memory read|write");
            }
        } else if (cmd == "x") {
            auto addr = need_loc(1);
            std::size_t n = args.size() > 2 ? need_value(2) : 16;
            auto bytes = read_bytes(addr, n);
            for (std::size_t i = 0; i < bytes.size(); ++i) {
                if (i % 16 == 0) out << (i ? "\n" : "") << hex(addr + i) << ":";
                out << " " << std::hex << std::setw(2) << std::setfill('0') << int(bytes[i]) << std::dec;
            }
            out << std::setfill(' ') << "\n";
        } else if (is_prefix(cmd, "where")) {
            require_alive();
            std::uint64_t pc = get_pc();
            out << hex(pc);
            if (auto d = describe(pc); !d.empty()) out << " <" << d << ">";
            out << "\n";
        } else if (is_prefix(cmd, "symbols")) {
            std::string filter = args.size() > 1 ? args[1] : "";
            for (const auto& s : m_symbols.all())
                if (s.name.find(filter) != std::string::npos)
                    out << hex(m_load_address + s.value) << "  "
                        << (s.type == symbol_type::function ? "func  " : "object") << "  " << s.name << "\n";
        } else {
            out << "Unknown command '" << cmd << "'. Type 'help'.\n";
        }
    } catch (const std::exception& e) {
        out << "error: " << e.what() << "\n";
    }
    return true;
}

void debugger::run(std::istream& in, std::ostream& out) {
    std::string line, last;
    out << "minidbg> " << std::flush;
    while (std::getline(in, line)) {
        if (line.empty()) line = last;  // like gdb, Enter repeats the previous command
        last = line;
        if (!handle_command(line, out)) break;
        out << "minidbg> " << std::flush;
    }
}

}  // namespace minidbg
