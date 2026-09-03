#include "parser.h"

#include <stdexcept>

namespace {

class Parser {
public:
    explicit Parser(const std::vector<Token>& toks) : toks_(toks) {}

    Program parseProgram() {
        Program prog;
        while (!check(TokKind::End)) {
            prog.functions.push_back(parseFunction());
        }
        return prog;
    }

private:
    const std::vector<Token>& toks_;
    size_t pos_ = 0;

    const Token& peek(size_t off = 0) const {
        size_t idx = pos_ + off;
        return idx < toks_.size() ? toks_[idx] : toks_.back();
    }
    const Token& advance() { const Token& t = peek(); if (!check(TokKind::End)) pos_++; return t; }
    bool check(TokKind k) const { return peek().kind == k; }
    bool match(TokKind k) { if (check(k)) { advance(); return true; } return false; }

    [[noreturn]] void error(const std::string& msg) const {
        throw std::runtime_error("line " + std::to_string(peek().line) + ": " + msg);
    }

    const Token& expect(TokKind k, const std::string& what) {
        if (!check(k)) error("expected " + what + ", got '" + peek().text + "'");
        return advance();
    }

    bool isTypeKeyword(TokKind k) const { return k == TokKind::KwInt || k == TokKind::KwVoid; }

    // ---- top level ----

    std::unique_ptr<FunctionDecl> parseFunction() {
        int line = peek().line;
        match(TokKind::KwExtern);  // 'extern' is accepted and ignored: every declaration
                                    // without a body already behaves like one.
        if (!isTypeKeyword(peek().kind)) error("expected a function return type ('int' or 'void')");
        advance();  // return type -- not tracked, this compiler has one value type (int)

        std::string name = expect(TokKind::Ident, "function name").text;
        expect(TokKind::LParen, "'('");

        auto fn = std::make_unique<FunctionDecl>();
        fn->name = name;
        fn->line = line;

        if (!check(TokKind::RParen)) {
            if (check(TokKind::KwVoid) && peek(1).kind == TokKind::RParen) {
                advance();  // int foo(void)
            } else {
                do {
                    expect(TokKind::KwInt, "'int' (parameter type)");
                    Param p;
                    p.line = peek().line;
                    p.name = expect(TokKind::Ident, "parameter name").text;
                    fn->params.push_back(std::move(p));
                } while (match(TokKind::Comma));
            }
        }
        expect(TokKind::RParen, "')'");

        if (match(TokKind::Semicolon)) {
            fn->body = nullptr;  // prototype only
        } else {
            fn->body = parseBlock();
        }
        return fn;
    }

    // ---- statements ----

    StmtPtr parseBlock() {
        int line = expect(TokKind::LBrace, "'{'").line;
        auto block = std::make_unique<BlockStmt>(line);
        while (!check(TokKind::RBrace) && !check(TokKind::End)) {
            block->items.push_back(parseBlockItem());
        }
        expect(TokKind::RBrace, "'}'");
        return block;
    }

    StmtPtr parseBlockItem() {
        if (check(TokKind::KwInt)) return parseVarDecl();
        return parseStatement();
    }

    StmtPtr parseVarDecl() {
        int line = expect(TokKind::KwInt, "'int'").line;
        std::string name = expect(TokKind::Ident, "variable name").text;
        ExprPtr init;
        if (match(TokKind::Assign)) init = parseExpr();
        expect(TokKind::Semicolon, "';'");
        return std::make_unique<VarDeclStmt>(name, std::move(init), line);
    }

    StmtPtr parseStatement() {
        int line = peek().line;
        switch (peek().kind) {
            case TokKind::LBrace:
                return parseBlock();
            case TokKind::Semicolon:
                advance();
                return std::make_unique<EmptyStmt>(line);
            case TokKind::KwReturn: {
                advance();
                ExprPtr value;
                if (!check(TokKind::Semicolon)) value = parseExpr();
                expect(TokKind::Semicolon, "';' after return");
                return std::make_unique<ReturnStmt>(std::move(value), line);
            }
            case TokKind::KwBreak:
                advance();
                expect(TokKind::Semicolon, "';' after break");
                return std::make_unique<BreakStmt>(line);
            case TokKind::KwContinue:
                advance();
                expect(TokKind::Semicolon, "';' after continue");
                return std::make_unique<ContinueStmt>(line);
            case TokKind::KwIf: {
                advance();
                expect(TokKind::LParen, "'(' after if");
                ExprPtr cond = parseExpr();
                expect(TokKind::RParen, "')'");
                StmtPtr thenBranch = parseStatement();
                StmtPtr elseBranch;
                if (match(TokKind::KwElse)) elseBranch = parseStatement();
                return std::make_unique<IfStmt>(std::move(cond), std::move(thenBranch), std::move(elseBranch), line);
            }
            case TokKind::KwWhile: {
                advance();
                expect(TokKind::LParen, "'(' after while");
                ExprPtr cond = parseExpr();
                expect(TokKind::RParen, "')'");
                StmtPtr body = parseStatement();
                return std::make_unique<WhileStmt>(std::move(cond), std::move(body), line);
            }
            case TokKind::KwFor: {
                advance();
                expect(TokKind::LParen, "'(' after for");
                StmtPtr init;
                if (check(TokKind::KwInt)) {
                    init = parseVarDecl();  // consumes its own trailing ';'
                } else if (!check(TokKind::Semicolon)) {
                    ExprPtr e = parseExpr();
                    init = std::make_unique<ExprStmt>(std::move(e), line);
                    expect(TokKind::Semicolon, "';'");
                } else {
                    advance();  // bare ';'
                }
                ExprPtr cond;
                if (!check(TokKind::Semicolon)) cond = parseExpr();
                expect(TokKind::Semicolon, "';' after for-condition");
                ExprPtr post;
                if (!check(TokKind::RParen)) post = parseExpr();
                expect(TokKind::RParen, "')'");
                StmtPtr body = parseStatement();
                return std::make_unique<ForStmt>(std::move(init), std::move(cond), std::move(post), std::move(body), line);
            }
            default: {
                ExprPtr e = parseExpr();
                expect(TokKind::Semicolon, "';' after expression");
                return std::make_unique<ExprStmt>(std::move(e), line);
            }
        }
    }

    // ---- expressions (precedence climbing, one function per level) ----

    ExprPtr parseExpr() { return parseAssignment(); }

    ExprPtr parseAssignment() {
        // Only `identifier = ...` is a valid assignment target in this language
        // (no pointers/lvalue expressions), so a one-token lookahead suffices.
        if (check(TokKind::Ident) && peek(1).kind == TokKind::Assign) {
            int line = peek().line;
            std::string name = advance().text;
            advance();  // '='
            ExprPtr value = parseAssignment();
            return std::make_unique<AssignExpr>(name, std::move(value), line);
        }
        return parseLogicalOr();
    }

    ExprPtr parseLogicalOr() {
        ExprPtr left = parseLogicalAnd();
        while (check(TokKind::OrOr)) {
            int line = advance().line;
            ExprPtr right = parseLogicalAnd();
            left = std::make_unique<LogicalExpr>(LogicalOp::Or, std::move(left), std::move(right), line);
        }
        return left;
    }

    ExprPtr parseLogicalAnd() {
        ExprPtr left = parseBitOr();
        while (check(TokKind::AndAnd)) {
            int line = advance().line;
            ExprPtr right = parseBitOr();
            left = std::make_unique<LogicalExpr>(LogicalOp::And, std::move(left), std::move(right), line);
        }
        return left;
    }

    ExprPtr parseBitOr() {
        ExprPtr left = parseBitXor();
        while (check(TokKind::Pipe)) {
            int line = advance().line;
            ExprPtr right = parseBitXor();
            left = std::make_unique<BinaryExpr>(BinaryOp::BitOr, std::move(left), std::move(right), line);
        }
        return left;
    }

    ExprPtr parseBitXor() {
        ExprPtr left = parseBitAnd();
        while (check(TokKind::Caret)) {
            int line = advance().line;
            ExprPtr right = parseBitAnd();
            left = std::make_unique<BinaryExpr>(BinaryOp::BitXor, std::move(left), std::move(right), line);
        }
        return left;
    }

    ExprPtr parseBitAnd() {
        ExprPtr left = parseEquality();
        while (check(TokKind::Amp)) {
            int line = advance().line;
            ExprPtr right = parseEquality();
            left = std::make_unique<BinaryExpr>(BinaryOp::BitAnd, std::move(left), std::move(right), line);
        }
        return left;
    }

    ExprPtr parseEquality() {
        ExprPtr left = parseRelational();
        while (check(TokKind::Eq) || check(TokKind::Ne)) {
            BinaryOp op = check(TokKind::Eq) ? BinaryOp::Eq : BinaryOp::Ne;
            int line = advance().line;
            ExprPtr right = parseRelational();
            left = std::make_unique<BinaryExpr>(op, std::move(left), std::move(right), line);
        }
        return left;
    }

    ExprPtr parseRelational() {
        ExprPtr left = parseAdditive();
        while (check(TokKind::Lt) || check(TokKind::Le) || check(TokKind::Gt) || check(TokKind::Ge)) {
            BinaryOp op = check(TokKind::Lt) ? BinaryOp::Lt
                        : check(TokKind::Le) ? BinaryOp::Le
                        : check(TokKind::Gt) ? BinaryOp::Gt
                                              : BinaryOp::Ge;
            int line = advance().line;
            ExprPtr right = parseAdditive();
            left = std::make_unique<BinaryExpr>(op, std::move(left), std::move(right), line);
        }
        return left;
    }

    ExprPtr parseAdditive() {
        ExprPtr left = parseTerm();
        while (check(TokKind::Plus) || check(TokKind::Minus)) {
            BinaryOp op = check(TokKind::Plus) ? BinaryOp::Add : BinaryOp::Sub;
            int line = advance().line;
            ExprPtr right = parseTerm();
            left = std::make_unique<BinaryExpr>(op, std::move(left), std::move(right), line);
        }
        return left;
    }

    ExprPtr parseTerm() {
        ExprPtr left = parseUnary();
        while (check(TokKind::Star) || check(TokKind::Slash) || check(TokKind::Percent)) {
            BinaryOp op = check(TokKind::Star) ? BinaryOp::Mul : check(TokKind::Slash) ? BinaryOp::Div : BinaryOp::Mod;
            int line = advance().line;
            ExprPtr right = parseUnary();
            left = std::make_unique<BinaryExpr>(op, std::move(left), std::move(right), line);
        }
        return left;
    }

    ExprPtr parseUnary() {
        if (check(TokKind::Minus) || check(TokKind::Bang) || check(TokKind::Tilde)) {
            UnaryOp op = check(TokKind::Minus) ? UnaryOp::Neg : check(TokKind::Bang) ? UnaryOp::Not : UnaryOp::BitNot;
            int line = advance().line;
            ExprPtr operand = parseUnary();
            return std::make_unique<UnaryExpr>(op, std::move(operand), line);
        }
        return parsePrimary();
    }

    ExprPtr parsePrimary() {
        int line = peek().line;
        if (check(TokKind::IntLit)) {
            long v = advance().value;
            return std::make_unique<IntLitExpr>(v, line);
        }
        if (check(TokKind::LParen)) {
            advance();
            ExprPtr e = parseExpr();
            expect(TokKind::RParen, "')'");
            return e;
        }
        if (check(TokKind::Ident)) {
            std::string name = advance().text;
            if (match(TokKind::LParen)) {
                std::vector<ExprPtr> args;
                if (!check(TokKind::RParen)) {
                    do {
                        args.push_back(parseExpr());
                    } while (match(TokKind::Comma));
                }
                expect(TokKind::RParen, "')'");
                return std::make_unique<CallExpr>(name, std::move(args), line);
            }
            return std::make_unique<VarRefExpr>(name, line);
        }
        error("expected an expression, got '" + peek().text + "'");
    }
};

}  // namespace

Program parse(const std::vector<Token>& tokens) {
    Parser p(tokens);
    return p.parseProgram();
}
