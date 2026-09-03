#pragma once

#include "ast.h"

// Walks the whole program once, in place:
//  - registers every function's name+arity (so forward calls and recursion
//    work no matter the source order) and rejects arity mismatches/redefinitions
//  - assigns each parameter and local variable a fixed stack-frame offset,
//    honoring C block scoping (shadowing allowed, redeclaration in the same
//    scope is not), and records each identifier reference's resolved offset
//    directly on the AST node
//  - computes each function's total frame size (16-byte aligned)
//  - rejects break/continue outside a loop and calls to unknown functions
// Throws std::runtime_error (line-prefixed) on any violation.
void resolveProgram(Program& program);
