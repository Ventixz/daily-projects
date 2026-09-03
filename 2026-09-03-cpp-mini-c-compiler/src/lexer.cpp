#include "lexer.h"

#include <cctype>
#include <stdexcept>
#include <unordered_map>

namespace {

const std::unordered_map<std::string, TokKind> kKeywords = {
    {"int", TokKind::KwInt},       {"return", TokKind::KwReturn},
    {"if", TokKind::KwIf},         {"else", TokKind::KwElse},
    {"while", TokKind::KwWhile},   {"for", TokKind::KwFor},
    {"break", TokKind::KwBreak},   {"continue", TokKind::KwContinue},
    {"extern", TokKind::KwExtern}, {"void", TokKind::KwVoid},
};

[[noreturn]] void fail(int line, const std::string& msg) {
    throw std::runtime_error("line " + std::to_string(line) + ": " + msg);
}

}  // namespace

std::vector<Token> lex(const std::string& src) {
    std::vector<Token> toks;
    size_t i = 0;
    int line = 1;
    const size_t n = src.size();

    auto peek = [&](size_t off = 0) -> char { return i + off < n ? src[i + off] : '\0'; };

    while (i < n) {
        char c = src[i];

        if (c == '\n') { line++; i++; continue; }
        if (std::isspace(static_cast<unsigned char>(c))) { i++; continue; }

        // Comments: // line and /* block */
        if (c == '/' && peek(1) == '/') {
            while (i < n && src[i] != '\n') i++;
            continue;
        }
        if (c == '/' && peek(1) == '*') {
            size_t start_line = line;
            i += 2;
            while (i + 1 < n && !(src[i] == '*' && src[i + 1] == '/')) {
                if (src[i] == '\n') line++;
                i++;
            }
            if (i + 1 >= n) fail(static_cast<int>(start_line), "unterminated block comment");
            i += 2;
            continue;
        }

        if (std::isdigit(static_cast<unsigned char>(c))) {
            size_t start = i;
            while (i < n && std::isdigit(static_cast<unsigned char>(src[i]))) i++;
            if (i < n && (std::isalpha(static_cast<unsigned char>(src[i])) || src[i] == '_')) {
                fail(line, "invalid number suffix near '" + src.substr(start, i - start + 1) + "'");
            }
            std::string text = src.substr(start, i - start);
            long value;
            try {
                value = std::stol(text);
            } catch (const std::out_of_range&) {
                fail(line, "integer literal '" + text + "' is out of range");
            }
            // Every literal has to fit in a 32-bit `mov eax, imm32` -- this compiler's
            // only type is a 32-bit int, so anything bigger can never be represented.
            if (value > 0xFFFFFFFFL) fail(line, "integer literal '" + text + "' is out of range");
            toks.push_back(Token{TokKind::IntLit, text, value, line});
            continue;
        }

        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            size_t start = i;
            while (i < n && (std::isalnum(static_cast<unsigned char>(src[i])) || src[i] == '_')) i++;
            std::string text = src.substr(start, i - start);
            auto it = kKeywords.find(text);
            TokKind kind = it != kKeywords.end() ? it->second : TokKind::Ident;
            toks.push_back(Token{kind, text, 0, line});
            continue;
        }

        auto two = [&](char a, char b, TokKind kind) -> bool {
            if (c == a && peek(1) == b) {
                toks.push_back(Token{kind, std::string(1, a) + b, 0, line});
                i += 2;
                return true;
            }
            return false;
        };

        if (two('=', '=', TokKind::Eq)) continue;
        if (two('!', '=', TokKind::Ne)) continue;
        if (two('<', '=', TokKind::Le)) continue;
        if (two('>', '=', TokKind::Ge)) continue;
        if (two('&', '&', TokKind::AndAnd)) continue;
        if (two('|', '|', TokKind::OrOr)) continue;

        TokKind single;
        switch (c) {
            case '(': single = TokKind::LParen; break;
            case ')': single = TokKind::RParen; break;
            case '{': single = TokKind::LBrace; break;
            case '}': single = TokKind::RBrace; break;
            case ';': single = TokKind::Semicolon; break;
            case ',': single = TokKind::Comma; break;
            case '+': single = TokKind::Plus; break;
            case '-': single = TokKind::Minus; break;
            case '*': single = TokKind::Star; break;
            case '/': single = TokKind::Slash; break;
            case '%': single = TokKind::Percent; break;
            case '=': single = TokKind::Assign; break;
            case '<': single = TokKind::Lt; break;
            case '>': single = TokKind::Gt; break;
            case '!': single = TokKind::Bang; break;
            case '&': single = TokKind::Amp; break;
            case '|': single = TokKind::Pipe; break;
            case '^': single = TokKind::Caret; break;
            case '~': single = TokKind::Tilde; break;
            default:
                fail(line, std::string("unexpected character '") + c + "'");
        }
        toks.push_back(Token{single, std::string(1, c), 0, line});
        i++;
    }

    toks.push_back(Token{TokKind::End, "", 0, line});
    return toks;
}
