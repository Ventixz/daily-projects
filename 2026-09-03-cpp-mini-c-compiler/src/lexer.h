#pragma once

#include <string>
#include <vector>

enum class TokKind {
    // literals / names
    IntLit, Ident,
    // keywords
    KwInt, KwReturn, KwIf, KwElse, KwWhile, KwFor, KwBreak, KwContinue, KwExtern, KwVoid,
    // punctuation
    LParen, RParen, LBrace, RBrace, Semicolon, Comma,
    // operators
    Plus, Minus, Star, Slash, Percent,
    Assign, Eq, Ne, Lt, Le, Gt, Ge,
    AndAnd, OrOr, Bang,
    Amp, Pipe, Caret, Tilde,
    End,
};

struct Token {
    TokKind kind;
    std::string text;   // raw text (identifier name, or the literal digits)
    long value = 0;     // for IntLit
    int line = 0;
};

// Tokenizes an entire mini-C source string up front. Throws std::runtime_error
// (with a "line N: ..." prefixed message) on any character or literal it can't
// make sense of -- there is no error recovery, the whole point is a clear stop.
std::vector<Token> lex(const std::string& source);
