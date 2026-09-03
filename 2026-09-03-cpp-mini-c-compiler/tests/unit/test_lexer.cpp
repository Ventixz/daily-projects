#include <iostream>
#include <stdexcept>
#include <string>

#include "../../src/lexer.h"

namespace {
int g_total = 0;
int g_failures = 0;
}  // namespace

#define CHECK(cond)                                                                        \
    do {                                                                                    \
        g_total++;                                                                          \
        if (!(cond)) {                                                                      \
            g_failures++;                                                                   \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << ": " << #cond << "\n";   \
        }                                                                                    \
    } while (0)

static void test_empty_source_is_just_end() {
    auto toks = lex("");
    CHECK(toks.size() == 1);
    CHECK(toks[0].kind == TokKind::End);
}

static void test_keywords_vs_identifiers() {
    auto toks = lex("int return if else while for break continue extern void foo");
    std::vector<TokKind> want = {TokKind::KwInt,  TokKind::KwReturn, TokKind::KwIf,      TokKind::KwElse,
                                  TokKind::KwWhile, TokKind::KwFor,    TokKind::KwBreak,   TokKind::KwContinue,
                                  TokKind::KwExtern, TokKind::KwVoid,  TokKind::Ident,     TokKind::End};
    CHECK(toks.size() == want.size());
    for (size_t i = 0; i < want.size() && i < toks.size(); i++) CHECK(toks[i].kind == want[i]);
    CHECK(toks[10].text == "foo");
}

static void test_integer_literal_value() {
    auto toks = lex("42 0 1000000");
    CHECK(toks[0].kind == TokKind::IntLit && toks[0].value == 42);
    CHECK(toks[1].kind == TokKind::IntLit && toks[1].value == 0);
    CHECK(toks[2].kind == TokKind::IntLit && toks[2].value == 1000000);
}

static void test_two_char_operators_are_not_split() {
    auto toks = lex("== != <= >= && ||");
    std::vector<TokKind> want = {TokKind::Eq, TokKind::Ne, TokKind::Le, TokKind::Ge, TokKind::AndAnd, TokKind::OrOr};
    for (size_t i = 0; i < want.size(); i++) CHECK(toks[i].kind == want[i]);
}

static void test_single_char_vs_two_char_disambiguation() {
    // '=' followed by something else must not be swallowed into '=='.
    auto toks = lex("= == = !=");
    CHECK(toks[0].kind == TokKind::Assign);
    CHECK(toks[1].kind == TokKind::Eq);
    CHECK(toks[2].kind == TokKind::Assign);
    CHECK(toks[3].kind == TokKind::Ne);
}

static void test_comments_are_skipped() {
    auto toks = lex("1 // trailing comment with == and { garbage\n2 /* block\nspanning lines */ 3");
    CHECK(toks[0].value == 1);
    CHECK(toks[1].value == 2);
    CHECK(toks[2].value == 3);
    CHECK(toks[3].kind == TokKind::End);
}

static void test_line_numbers_track_newlines() {
    auto toks = lex("1\n2\n\n3");
    CHECK(toks[0].line == 1);
    CHECK(toks[1].line == 2);
    CHECK(toks[2].line == 4);
}

static void test_unexpected_character_throws() {
    bool threw = false;
    try {
        lex("int x = 1 @ 2;");
    } catch (const std::runtime_error& e) {
        threw = true;
        std::string msg = e.what();
        CHECK(msg.find("line 1") != std::string::npos);
    }
    CHECK(threw);
}

static void test_oversized_literal_throws() {
    bool threw = false;
    try {
        lex("99999999999999");
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
}

int main() {
    test_empty_source_is_just_end();
    test_keywords_vs_identifiers();
    test_integer_literal_value();
    test_two_char_operators_are_not_split();
    test_single_char_vs_two_char_disambiguation();
    test_comments_are_skipped();
    test_line_numbers_track_newlines();
    test_unexpected_character_throws();
    test_oversized_literal_throws();

    std::cout << "test_lexer: " << (g_total - g_failures) << "/" << g_total << " passed\n";
    return g_failures == 0 ? 0 : 1;
}
