// test_debugger.cpp - drives real processes (build/target, build/target-nopie, build/crash) under minidbg.
#include <signal.h>

#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "debugger.hpp"

using namespace minidbg;

namespace {

int g_failed = 0;
int g_checks = 0;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        ++g_checks;                                                                    \
        if (!(cond)) {                                                                 \
            ++g_failed;                                                                \
            std::cerr << "    FAIL " << __FILE__ << ":" << __LINE__ << ": " #cond "\n"; \
        }                                                                              \
    } while (0)

const char* PIE = "build/target";
const char* NOPIE = "build/target-nopie";

std::string run_cmd(debugger& d, const std::string& cmd) {
    std::ostringstream out;
    d.handle_command(cmd, out);
    return out.str();
}

bool contains(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

std::vector<std::pair<std::string, std::function<void()>>> tests = {

    {"ELF symbols: finds main/add/counter, detects PIE", [] {
        elf_symbols pie(PIE), nopie(NOPIE);
        CHECK(pie.is_pie());
        CHECK(!nopie.is_pie());
        CHECK(pie.find("main") && pie.find("main")->type == symbol_type::function);
        CHECK(pie.find("add") && pie.find("add")->size > 0);
        CHECK(pie.find("counter") && pie.find("counter")->type == symbol_type::object);
        CHECK(!pie.find("no_such_symbol"));
        auto add = *nopie.find("add");
        CHECK(nopie.function_at(add.value + 1)->name == "add");
    }},

    {"register table matches user_regs_struct names", [] {
        CHECK(get_register_from_name("rip") == reg::rip);
        CHECK(get_register_from_name("eflags") == reg::rflags);
        CHECK(get_register_from_name("rflags") == reg::rflags);
        CHECK(!get_register_from_name("xmm0"));
        CHECK(get_register_name(reg::rdi) == "rdi");
        CHECK(get_register_from_dwarf(5) == reg::rdi);  // System V DWARF numbering
    }},

    {"launch stops before the first instruction; continue runs to exit", [] {
        debugger d(PIE);
        CHECK(d.alive());
        CHECK(d.load_address() == 0x555555554000ULL);  // ASLR off in the child
        auto ev = d.continue_execution();
        CHECK(ev.what == stop_event::kind::exited);
        CHECK(ev.code == 6);
        CHECK(!d.alive());
    }},

    {"breakpoint fires on every call and stepping over it keeps the program correct", [] {
        for (const char* prog : {PIE, NOPIE}) {
            debugger d(prog);
            auto add = *d.resolve("add");
            d.set_breakpoint_at_address(add);
            int hits = 0;
            stop_event ev;
            while ((ev = d.continue_execution()).what == stop_event::kind::breakpoint) {
                CHECK(ev.pc == add);
                CHECK(d.get_pc() == add);  // rip was wound back past the int3
                ++hits;
            }
            CHECK(hits == 3);
            CHECK(ev.what == stop_event::kind::exited);
            CHECK(ev.code == 6);  // original instruction ran each time
        }
    }},

    {"registers: read arguments at the breakpoint, write one to change the result", [] {
        debugger d(PIE);
        d.set_breakpoint_at_address(*d.resolve("add"));
        d.continue_execution();
        CHECK(get_register_value(d.pid(), reg::rdi) == 0);  // add(counter=0, i=1)
        CHECK(get_register_value(d.pid(), reg::rsi) == 1);
        d.continue_execution();
        CHECK(get_register_value(d.pid(), reg::rdi) == 1);  // add(1, 2)
        CHECK(get_register_value(d.pid(), reg::rsi) == 2);
        set_register_value(d.pid(), reg::rsi, 100);       // add(1, 100) -> 101, then +3
        d.remove_breakpoint(*d.resolve("add"));
        auto ev = d.continue_execution();
        CHECK(ev.what == stop_event::kind::exited);
        CHECK(ev.code == 104);
    }},

    {"memory: read and write a global through its symbol", [] {
        debugger d(NOPIE);
        auto counter = *d.resolve("counter");
        d.set_breakpoint_at_address(*d.resolve("add"));
        d.continue_execution();
        d.continue_execution();
        CHECK(d.read_memory(counter) == 1);  // second call: counter already 0+1
        d.remove_breakpoint(*d.resolve("add"));
        // We are inside the second call: add(1, 2) already has its arguments
        // in registers, and main stores the result into counter on return.
        // So this write is overwritten straight away - a debugger edits state,
        // it doesn't rewind the program's logic.
        d.write_memory(counter, 40);
        CHECK(d.read_memory(counter) == 40);
        auto ev = d.continue_execution();
        CHECK(ev.code == 6);
    }},

    {"memory write before the loop changes the program's answer", [] {
        debugger d(PIE);
        d.set_breakpoint_at_address(*d.resolve("main"));
        CHECK(d.continue_execution().what == stop_event::kind::breakpoint);
        d.write_memory(*d.resolve("counter"), 40);
        auto ev = d.continue_execution();
        CHECK(ev.code == 46);
    }},

    {"read_bytes hides the int3 patch, raw memory shows it", [] {
        debugger d(PIE);
        auto main = *d.resolve("main");
        auto before = d.read_bytes(main, 4);
        d.set_breakpoint_at_address(main);
        CHECK((d.read_memory(main) & 0xff) == 0xcc);
        CHECK(d.read_bytes(main, 4) == before);
        CHECK(d.read_bytes(main + 1, 3) == std::vector<std::uint8_t>(before.begin() + 1, before.end()));
        d.remove_breakpoint(main);
        CHECK((d.read_memory(main) & 0xff) == before[0]);
    }},

    {"stepi moves one instruction, including off a breakpoint", [] {
        debugger d(PIE);
        auto add = *d.resolve("add");
        d.set_breakpoint_at_address(add);
        d.continue_execution();
        auto ev = d.single_step();
        CHECK(ev.what == stop_event::kind::step);
        CHECK(ev.pc > add && ev.pc < add + 8);  // a single x86 instruction later
        CHECK(d.breakpoints().at(add).is_enabled());  // re-armed
        auto pc = d.get_pc();
        CHECK(d.single_step().pc > pc);
        int hits = 0;
        while (d.continue_execution().what == stop_event::kind::breakpoint) ++hits;
        CHECK(hits == 2);
        CHECK(d.exit_code() == 6);
    }},

    {"signals are reported and passed on to the program", [] {
        debugger d("build/crash");
        auto ev = d.continue_execution();
        CHECK(ev.what == stop_event::kind::signal);
        CHECK(ev.code == SIGSEGV);
        ev = d.continue_execution();  // deliver it: default action kills the process
        CHECK(ev.what == stop_event::kind::killed);
        CHECK(ev.code == SIGSEGV);
    }},

    {"command interface", [] {
        debugger d(PIE);
        CHECK(contains(run_cmd(d, "break add"), "Breakpoint set at 0x"));
        CHECK(contains(run_cmd(d, "info"), "<add>"));
        CHECK(contains(run_cmd(d, "c"), "Breakpoint hit at"));
        CHECK(contains(run_cmd(d, "where"), "<add>"));
        CHECK(contains(run_cmd(d, "reg dump"), "rip"));
        CHECK(contains(run_cmd(d, "register read rsi"), "0x0000000000000001"));
        run_cmd(d, "register write rsi 0x10");
        CHECK(contains(run_cmd(d, "reg read rsi"), "0x0000000000000010"));
        CHECK(contains(run_cmd(d, "memory read counter"), "0x0000000000000000"));
        CHECK(contains(run_cmd(d, "x add 4"), ":"));
        CHECK(contains(run_cmd(d, "si"), "<add+"));
        CHECK(contains(run_cmd(d, "symbols coun"), "object  counter"));
        CHECK(contains(run_cmd(d, "break nowhere"), "error: unknown location"));
        CHECK(contains(run_cmd(d, "register read xmm9"), "error: unknown register"));
        CHECK(contains(run_cmd(d, "frobnicate"), "Unknown command"));
        CHECK(contains(run_cmd(d, "delete add"), "Deleted"));
        CHECK(contains(run_cmd(d, "continue"), "exited with code 21"));  // 0+16, +2, +3
        CHECK(contains(run_cmd(d, "continue"), "error: the program is not running"));
        std::ostringstream o;
        CHECK(!d.handle_command("quit", o));
    }},

    {"REPL repeats the last command on an empty line", [] {
        debugger d(PIE);
        std::istringstream in("break add\ncontinue\n\n\nquit\n");
        std::ostringstream out;
        d.run(in, out);
        std::string s = out.str();
        std::size_t hits = 0;
        for (std::size_t p = 0; (p = s.find("Breakpoint hit", p)) != std::string::npos; ++p) ++hits;
        CHECK(hits == 3);
    }},

    {"bad program path is an error, not a hang", [] {
        bool threw = false;
        try { debugger d("build/does-not-exist"); } catch (const std::exception&) { threw = true; }
        CHECK(threw);
    }},
};

}  // namespace

int main() {
    int failed_tests = 0;
    for (auto& [name, fn] : tests) {
        int before = g_failed;
        try {
            fn();
        } catch (const std::exception& e) {
            ++g_failed;
            std::cerr << "    exception: " << e.what() << "\n";
        }
        bool ok = g_failed == before;
        failed_tests += !ok;
        std::cout << (ok ? "PASS " : "FAIL ") << name << "\n";
    }
    std::cout << "\n" << tests.size() - failed_tests << "/" << tests.size() << " tests passed (" << g_checks
              << " checks)\n";
    return failed_tests ? 1 : 0;
}
