#pragma once

#include <memory>
#include <string>
#include <vector>

// ---- Expressions -----------------------------------------------------------

struct Expr {
    enum class Kind { IntLit, VarRef, Assign, Unary, Binary, Logical, Call };
    Kind kind;
    int line;
    // Filled in by the resolver for VarRef/Assign: byte offset from rbp (always
    // negative). Left at 0 until resolved.
    int slotOffset = 0;

    explicit Expr(Kind k, int ln) : kind(k), line(ln) {}
    virtual ~Expr() = default;
};
using ExprPtr = std::unique_ptr<Expr>;

struct IntLitExpr : Expr {
    long value;
    IntLitExpr(long v, int ln) : Expr(Kind::IntLit, ln), value(v) {}
};

struct VarRefExpr : Expr {
    std::string name;
    VarRefExpr(std::string n, int ln) : Expr(Kind::VarRef, ln), name(std::move(n)) {}
};

struct AssignExpr : Expr {
    std::string name;
    ExprPtr value;
    AssignExpr(std::string n, ExprPtr v, int ln)
        : Expr(Kind::Assign, ln), name(std::move(n)), value(std::move(v)) {}
};

enum class UnaryOp { Neg, Not, BitNot };
struct UnaryExpr : Expr {
    UnaryOp op;
    ExprPtr operand;
    UnaryExpr(UnaryOp o, ExprPtr e, int ln) : Expr(Kind::Unary, ln), op(o), operand(std::move(e)) {}
};

enum class BinaryOp { Add, Sub, Mul, Div, Mod, Eq, Ne, Lt, Le, Gt, Ge, BitAnd, BitOr, BitXor };
struct BinaryExpr : Expr {
    BinaryOp op;
    ExprPtr lhs, rhs;
    BinaryExpr(BinaryOp o, ExprPtr l, ExprPtr r, int ln)
        : Expr(Kind::Binary, ln), op(o), lhs(std::move(l)), rhs(std::move(r)) {}
};

enum class LogicalOp { And, Or };
struct LogicalExpr : Expr {
    LogicalOp op;
    ExprPtr lhs, rhs;
    LogicalExpr(LogicalOp o, ExprPtr l, ExprPtr r, int ln)
        : Expr(Kind::Logical, ln), op(o), lhs(std::move(l)), rhs(std::move(r)) {}
};

struct CallExpr : Expr {
    std::string callee;
    std::vector<ExprPtr> args;
    CallExpr(std::string c, std::vector<ExprPtr> a, int ln)
        : Expr(Kind::Call, ln), callee(std::move(c)), args(std::move(a)) {}
};

// ---- Statements -------------------------------------------------------------

struct Stmt {
    enum class Kind { ExprStmt, VarDecl, Return, If, While, For, Break, Continue, Block, Empty };
    Kind kind;
    int line;
    explicit Stmt(Kind k, int ln) : kind(k), line(ln) {}
    virtual ~Stmt() = default;
};
using StmtPtr = std::unique_ptr<Stmt>;

struct ExprStmt : Stmt {
    ExprPtr expr;
    ExprStmt(ExprPtr e, int ln) : Stmt(Kind::ExprStmt, ln), expr(std::move(e)) {}
};

struct VarDeclStmt : Stmt {
    std::string name;
    ExprPtr init;  // nullable
    int slotOffset = 0;
    VarDeclStmt(std::string n, ExprPtr i, int ln)
        : Stmt(Kind::VarDecl, ln), name(std::move(n)), init(std::move(i)) {}
};

struct ReturnStmt : Stmt {
    ExprPtr value;  // nullable ("return;")
    ReturnStmt(ExprPtr v, int ln) : Stmt(Kind::Return, ln), value(std::move(v)) {}
};

struct BlockStmt : Stmt {
    std::vector<StmtPtr> items;
    explicit BlockStmt(int ln) : Stmt(Kind::Block, ln) {}
};

struct IfStmt : Stmt {
    ExprPtr cond;
    StmtPtr thenBranch, elseBranch;  // elseBranch nullable
    IfStmt(ExprPtr c, StmtPtr t, StmtPtr e, int ln)
        : Stmt(Kind::If, ln), cond(std::move(c)), thenBranch(std::move(t)), elseBranch(std::move(e)) {}
};

struct WhileStmt : Stmt {
    ExprPtr cond;
    StmtPtr body;
    WhileStmt(ExprPtr c, StmtPtr b, int ln) : Stmt(Kind::While, ln), cond(std::move(c)), body(std::move(b)) {}
};

struct ForStmt : Stmt {
    StmtPtr init;    // nullable: VarDeclStmt or ExprStmt
    ExprPtr cond;    // nullable (absent => always true)
    ExprPtr post;    // nullable
    StmtPtr body;
    ForStmt(StmtPtr i, ExprPtr c, ExprPtr p, StmtPtr b, int ln)
        : Stmt(Kind::For, ln), init(std::move(i)), cond(std::move(c)), post(std::move(p)), body(std::move(b)) {}
};

struct BreakStmt : Stmt { explicit BreakStmt(int ln) : Stmt(Kind::Break, ln) {} };
struct ContinueStmt : Stmt { explicit ContinueStmt(int ln) : Stmt(Kind::Continue, ln) {} };
struct EmptyStmt : Stmt { explicit EmptyStmt(int ln) : Stmt(Kind::Empty, ln) {} };

// ---- Top level ---------------------------------------------------------------

struct Param {
    std::string name;
    int line;
    int slotOffset = 0;
};

struct FunctionDecl {
    std::string name;
    std::vector<Param> params;
    StmtPtr body;  // null => it's a prototype/extern declaration, not a definition
    int frameSize = 0;
    int line;
};

struct Program {
    std::vector<std::unique_ptr<FunctionDecl>> functions;
};
