#include "resolver.h"

#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace {

constexpr int kMaxParams = 6;  // SysV integer-argument registers we use: rdi,rsi,rdx,rcx,r8,r9

[[noreturn]] void fail(int line, const std::string& msg) {
    throw std::runtime_error("line " + std::to_string(line) + ": " + msg);
}

struct FuncInfo {
    int arity;
    bool hasBody;
};

class Resolver {
public:
    void run(Program& program) {
        collectSignatures(program);
        for (auto& fn : program.functions) {
            if (fn->body) resolveFunction(*fn);
        }
    }

private:
    std::unordered_map<std::string, FuncInfo> functions_;
    std::vector<std::unordered_map<std::string, int>> scopes_;
    int currentOffset_ = 0;
    int maxOffsetMagnitude_ = 0;
    int loopDepth_ = 0;

    void collectSignatures(Program& program) {
        for (auto& fn : program.functions) {
            if (static_cast<int>(fn->params.size()) > kMaxParams) {
                fail(fn->line, "'" + fn->name + "' has more than " + std::to_string(kMaxParams) +
                                    " parameters, which this compiler doesn't support");
            }
            auto it = functions_.find(fn->name);
            if (it == functions_.end()) {
                functions_[fn->name] = FuncInfo{static_cast<int>(fn->params.size()), fn->body != nullptr};
            } else {
                if (it->second.arity != static_cast<int>(fn->params.size())) {
                    fail(fn->line, "conflicting declarations of '" + fn->name + "' with different parameter counts");
                }
                if (it->second.hasBody && fn->body) {
                    fail(fn->line, "redefinition of function '" + fn->name + "'");
                }
                it->second.hasBody = it->second.hasBody || (fn->body != nullptr);
            }
        }
    }

    void resolveFunction(FunctionDecl& fn) {
        scopes_.clear();
        scopes_.emplace_back();
        currentOffset_ = 0;
        maxOffsetMagnitude_ = 0;

        for (auto& param : fn.params) {
            declare(param.name, fn.line);
            param.slotOffset = currentOffset_;
        }

        // The function body's own block reuses this scope (params live in the
        // same scope real C would put them in) instead of pushing a new one.
        auto* block = static_cast<BlockStmt*>(fn.body.get());
        for (auto& item : block->items) resolveStmt(*item);

        int frame = maxOffsetMagnitude_;
        frame = (frame + 15) & ~15;  // round up to 16 bytes for SysV stack alignment
        fn.frameSize = frame;
    }

    // Allocates a new stack slot for `name` in the innermost scope.
    int declare(const std::string& name, int line) {
        auto& scope = scopes_.back();
        if (scope.count(name)) fail(line, "redeclaration of '" + name + "' in the same scope");
        currentOffset_ -= 8;
        if (-currentOffset_ > maxOffsetMagnitude_) maxOffsetMagnitude_ = -currentOffset_;
        scope[name] = currentOffset_;
        return currentOffset_;
    }

    int lookup(const std::string& name, int line) const {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) return found->second;
        }
        fail(line, "use of undeclared variable '" + name + "'");
    }

    void resolveStmt(Stmt& stmt) {
        switch (stmt.kind) {
            case Stmt::Kind::VarDecl: {
                auto& d = static_cast<VarDeclStmt&>(stmt);
                if (d.init) resolveExpr(*d.init);  // evaluated before the name enters scope
                d.slotOffset = declare(d.name, d.line);
                return;
            }
            case Stmt::Kind::ExprStmt:
                resolveExpr(*static_cast<ExprStmt&>(stmt).expr);
                return;
            case Stmt::Kind::Return: {
                auto& r = static_cast<ReturnStmt&>(stmt);
                if (r.value) resolveExpr(*r.value);
                return;
            }
            case Stmt::Kind::If: {
                auto& s = static_cast<IfStmt&>(stmt);
                resolveExpr(*s.cond);
                resolveStmt(*s.thenBranch);
                if (s.elseBranch) resolveStmt(*s.elseBranch);
                return;
            }
            case Stmt::Kind::While: {
                auto& s = static_cast<WhileStmt&>(stmt);
                resolveExpr(*s.cond);
                loopDepth_++;
                resolveStmt(*s.body);
                loopDepth_--;
                return;
            }
            case Stmt::Kind::For: {
                auto& s = static_cast<ForStmt&>(stmt);
                scopes_.emplace_back();  // the for-loop's own init variable is scoped to it
                if (s.init) resolveStmt(*s.init);
                if (s.cond) resolveExpr(*s.cond);
                if (s.post) resolveExpr(*s.post);
                loopDepth_++;
                resolveStmt(*s.body);
                loopDepth_--;
                scopes_.pop_back();
                return;
            }
            case Stmt::Kind::Break:
                if (loopDepth_ == 0) fail(stmt.line, "'break' outside of a loop");
                return;
            case Stmt::Kind::Continue:
                if (loopDepth_ == 0) fail(stmt.line, "'continue' outside of a loop");
                return;
            case Stmt::Kind::Block: {
                scopes_.emplace_back();
                for (auto& item : static_cast<BlockStmt&>(stmt).items) resolveStmt(*item);
                scopes_.pop_back();
                return;
            }
            case Stmt::Kind::Empty:
                return;
        }
    }

    void resolveExpr(Expr& expr) {
        switch (expr.kind) {
            case Expr::Kind::IntLit:
                return;
            case Expr::Kind::VarRef: {
                auto& v = static_cast<VarRefExpr&>(expr);
                expr.slotOffset = lookup(v.name, expr.line);
                return;
            }
            case Expr::Kind::Assign: {
                auto& a = static_cast<AssignExpr&>(expr);
                expr.slotOffset = lookup(a.name, expr.line);
                resolveExpr(*a.value);
                return;
            }
            case Expr::Kind::Unary:
                resolveExpr(*static_cast<UnaryExpr&>(expr).operand);
                return;
            case Expr::Kind::Binary: {
                auto& b = static_cast<BinaryExpr&>(expr);
                resolveExpr(*b.lhs);
                resolveExpr(*b.rhs);
                return;
            }
            case Expr::Kind::Logical: {
                auto& l = static_cast<LogicalExpr&>(expr);
                resolveExpr(*l.lhs);
                resolveExpr(*l.rhs);
                return;
            }
            case Expr::Kind::Call: {
                auto& c = static_cast<CallExpr&>(expr);
                auto it = functions_.find(c.callee);
                if (it == functions_.end()) fail(expr.line, "call to undeclared function '" + c.callee + "'");
                if (it->second.arity != static_cast<int>(c.args.size())) {
                    fail(expr.line, "'" + c.callee + "' expects " + std::to_string(it->second.arity) +
                                         " argument(s), got " + std::to_string(c.args.size()));
                }
                for (auto& arg : c.args) resolveExpr(*arg);
                return;
            }
        }
    }
};

}  // namespace

void resolveProgram(Program& program) {
    Resolver r;
    r.run(program);
}
