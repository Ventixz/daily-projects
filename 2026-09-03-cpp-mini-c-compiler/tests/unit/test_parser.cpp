#include <iostream>
#include <stdexcept>
#include <string>

#include "../../src/lexer.h"
#include "../../src/parser.h"
#include "../../src/resolver.h"

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

static Program parseSrc(const std::string& src) { return parse(lex(src)); }

static bool resolvesOk(const std::string& src) {
    try {
        Program p = parseSrc(src);
        resolveProgram(p);
        return true;
    } catch (const std::runtime_error&) {
        return false;
    }
}

static void test_parses_minimal_function() {
    Program p = parseSrc("int main() { return 0; }");
    CHECK(p.functions.size() == 1);
    CHECK(p.functions[0]->name == "main");
    CHECK(p.functions[0]->params.empty());
    CHECK(p.functions[0]->body != nullptr);
}

static void test_prototype_without_body() {
    Program p = parseSrc("extern void print_int(int x);");
    CHECK(p.functions.size() == 1);
    CHECK(p.functions[0]->body == nullptr);
    CHECK(p.functions[0]->params.size() == 1);
}

static void test_binary_precedence_shape() {
    // 1 + 2 * 3 must parse as 1 + (2 * 3): the top-level node is the '+'.
    Program p = parseSrc("int main() { return 1 + 2 * 3; }");
    auto* block = static_cast<BlockStmt*>(p.functions[0]->body.get());
    auto* ret = static_cast<ReturnStmt*>(block->items[0].get());
    CHECK(ret->value->kind == Expr::Kind::Binary);
    auto* top = static_cast<BinaryExpr*>(ret->value.get());
    CHECK(top->op == BinaryOp::Add);
    CHECK(top->rhs->kind == Expr::Kind::Binary);
    CHECK(static_cast<BinaryExpr*>(top->rhs.get())->op == BinaryOp::Mul);
    CHECK(top->lhs->kind == Expr::Kind::IntLit);
}

static void test_relational_binds_looser_than_additive() {
    // 1 + 2 < 3 must parse as (1 + 2) < 3.
    Program p = parseSrc("int main() { return 1 + 2 < 3; }");
    auto* block = static_cast<BlockStmt*>(p.functions[0]->body.get());
    auto* ret = static_cast<ReturnStmt*>(block->items[0].get());
    auto* top = static_cast<BinaryExpr*>(ret->value.get());
    CHECK(top->op == BinaryOp::Lt);
    CHECK(top->lhs->kind == Expr::Kind::Binary);
    CHECK(static_cast<BinaryExpr*>(top->lhs.get())->op == BinaryOp::Add);
}

static void test_assignment_is_right_associative_and_lowest_precedence() {
    Program p = parseSrc("int main() { int a; int b; a = b = 5; return a; }");
    auto* block = static_cast<BlockStmt*>(p.functions[0]->body.get());
    auto* stmt = static_cast<ExprStmt*>(block->items[2].get());
    CHECK(stmt->expr->kind == Expr::Kind::Assign);
    auto* outer = static_cast<AssignExpr*>(stmt->expr.get());
    CHECK(outer->name == "a");
    CHECK(outer->value->kind == Expr::Kind::Assign);
    CHECK(static_cast<AssignExpr*>(outer->value.get())->name == "b");
}

static void test_resolver_assigns_distinct_slots() {
    Program p = parseSrc("int main() { int a; int b; return a + b; }");
    resolveProgram(p);
    auto* block = static_cast<BlockStmt*>(p.functions[0]->body.get());
    int offA = static_cast<VarDeclStmt*>(block->items[0].get())->slotOffset;
    int offB = static_cast<VarDeclStmt*>(block->items[1].get())->slotOffset;
    CHECK(offA != offB);
    CHECK(offA < 0 && offB < 0);
    CHECK(p.functions[0]->frameSize >= 16);
    CHECK(p.functions[0]->frameSize % 16 == 0);
}

static void test_resolver_links_varref_to_declaration_slot() {
    Program p = parseSrc("int main() { int a; a = 5; return a; }");
    resolveProgram(p);
    auto* block = static_cast<BlockStmt*>(p.functions[0]->body.get());
    int declOff = static_cast<VarDeclStmt*>(block->items[0].get())->slotOffset;
    auto* assignStmt = static_cast<ExprStmt*>(block->items[1].get());
    CHECK(assignStmt->expr->slotOffset == declOff);
    auto* retStmt = static_cast<ReturnStmt*>(block->items[2].get());
    CHECK(retStmt->value->slotOffset == declOff);
}

static void test_shadowing_allowed_redeclaration_in_same_scope_rejected() {
    CHECK(resolvesOk("int main() { int a = 1; { int a = 2; } return a; }"));
    CHECK(!resolvesOk("int main() { int a = 1; int a = 2; return a; }"));
}

static void test_undeclared_variable_rejected() {
    CHECK(!resolvesOk("int main() { return x; }"));
}

static void test_break_continue_outside_loop_rejected() {
    CHECK(!resolvesOk("int main() { break; return 0; }"));
    CHECK(!resolvesOk("int main() { continue; return 0; }"));
    CHECK(resolvesOk("int main() { while (1) { break; continue; } return 0; }"));
}

static void test_call_arity_checked() {
    CHECK(resolvesOk("int add(int a, int b) { return a + b; } int main() { return add(1, 2); }"));
    CHECK(!resolvesOk("int add(int a, int b) { return a + b; } int main() { return add(1); }"));
    CHECK(!resolvesOk("int main() { return mystery(1); }"));
}

static void test_forward_and_mutual_recursion_resolve() {
    CHECK(resolvesOk(
        "int is_even(int n);"
        "int is_odd(int n) { if (n == 0) { return 0; } return is_even(n - 1); }"
        "int is_even(int n) { if (n == 0) { return 1; } return is_odd(n - 1); }"
        "int main() { return is_even(10); }"));
}

static void test_syntax_error_reports_line_number() {
    bool threw = false;
    try {
        parseSrc("int main() {\n  return 0\n}");  // missing ';'
    } catch (const std::runtime_error& e) {
        threw = true;
        std::string msg = e.what();
        CHECK(msg.find("line 3") != std::string::npos);  // the '}' that broke expectations
    }
    CHECK(threw);
}

int main() {
    test_parses_minimal_function();
    test_prototype_without_body();
    test_binary_precedence_shape();
    test_relational_binds_looser_than_additive();
    test_assignment_is_right_associative_and_lowest_precedence();
    test_resolver_assigns_distinct_slots();
    test_resolver_links_varref_to_declaration_slot();
    test_shadowing_allowed_redeclaration_in_same_scope_rejected();
    test_undeclared_variable_rejected();
    test_break_continue_outside_loop_rejected();
    test_call_arity_checked();
    test_forward_and_mutual_recursion_resolve();
    test_syntax_error_reports_line_number();

    std::cout << "test_parser: " << (g_total - g_failures) << "/" << g_total << " passed\n";
    return g_failures == 0 ? 0 : 1;
}
