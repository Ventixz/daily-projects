// main.cpp - minidbg <program> [args...]
#include <iostream>

#include "debugger.hpp"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " <program> [args...]\n";
        return 1;
    }
    try {
        std::vector<std::string> args(argv + 2, argv + argc);
        minidbg::debugger dbg(argv[1], args);
        std::cout << "Started " << argv[1] << " (pid " << dbg.pid() << "), stopped before its first instruction.\n"
                  << "Type 'help' for commands.\n";
        dbg.run(std::cin, std::cout);
    } catch (const std::exception& e) {
        std::cerr << "minidbg: " << e.what() << "\n";
        return 1;
    }
}
