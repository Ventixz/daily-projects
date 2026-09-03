#pragma once

#include <vector>

#include "ast.h"
#include "lexer.h"

// Recursive-descent parser for the mini-C subset. Throws std::runtime_error
// (line-prefixed) on any syntax error; there is no recovery.
Program parse(const std::vector<Token>& tokens);
