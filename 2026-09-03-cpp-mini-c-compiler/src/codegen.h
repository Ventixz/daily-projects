#pragma once

#include <ostream>

#include "ast.h"

// Emits x86-64 assembly (Intel syntax, GNU `as`-compatible) for an already-
// resolved Program (see resolver.h) to `out`. Assumes the program is valid --
// call resolveProgram() first.
void generateCode(const Program& program, std::ostream& out);
