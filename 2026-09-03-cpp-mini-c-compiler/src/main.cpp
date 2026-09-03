#include <fstream>
#include <iostream>
#include <sstream>

#include "codegen.h"
#include "lexer.h"
#include "parser.h"
#include "resolver.h"

namespace {

std::string readFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open '" + path + "'");
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: mcc <input.c> [-o <output.s>]\n";
        return 2;
    }

    std::string inputPath;
    std::string outputPath;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc) {
            outputPath = argv[++i];
        } else {
            inputPath = arg;
        }
    }

    try {
        std::string source = readFile(inputPath);
        auto tokens = lex(source);
        Program program = parse(tokens);
        resolveProgram(program);

        if (outputPath.empty()) {
            generateCode(program, std::cout);
        } else {
            std::ofstream out(outputPath);
            if (!out) throw std::runtime_error("cannot open '" + outputPath + "' for writing");
            generateCode(program, out);
        }
    } catch (const std::exception& e) {
        std::cerr << inputPath << ": error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
