#include "codegen.h"

#include <array>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

const std::array<const char*, 6> kArgReg32 = {"edi", "esi", "edx", "ecx", "r8d", "r9d"};
const std::array<const char*, 6> kArgReg64 = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};

class Codegen {
public:
    explicit Codegen(std::ostream& out) : out_(out) {}

    void emitProgram(const Program& program) {
        out_ << ".intel_syntax noprefix\n";
        for (auto& fn : program.functions) {
            if (fn->body) out_ << ".globl " << fn->name << "\n";
        }
        out_ << ".text\n";
        for (auto& fn : program.functions) {
            if (fn->body) emitFunction(*fn);
        }
        // Marks the object as not needing an executable stack; without it GNU ld
        // defaults to assuming the oldest, least safe behavior and warns.
        out_ << ".section .note.GNU-stack,\"\",@progbits\n";
    }

private:
    std::ostream& out_;
    int labelCounter_ = 0;
    int tempDepth_ = 0;  // # of live 8-byte pushes since the last 16-aligned point
    std::vector<std::pair<std::string, std::string>> loopStack_;  // {continueLabel, breakLabel}

    std::string newLabel(const std::string& prefix) { return "." + prefix + std::to_string(labelCounter_++); }

    static std::string slot(int offset) { return "[rbp" + std::to_string(offset) + "]"; }

    void push(const char* reg) {
        out_ << "    push " << reg << "\n";
        tempDepth_++;
    }
    void pop(const char* reg) {
        out_ << "    pop " << reg << "\n";
        tempDepth_--;
    }

    void emitFunction(const FunctionDecl& fn) {
        out_ << fn.name << ":\n";
        push("rbp");
        out_ << "    mov rbp, rsp\n";
        if (fn.frameSize > 0) out_ << "    sub rsp, " << fn.frameSize << "\n";
        tempDepth_ = 0;  // rsp is 16-aligned here: see the alignment note in emitCall()

        for (size_t i = 0; i < fn.params.size(); i++) {
            out_ << "    mov " << slot(fn.params[i].slotOffset) << ", " << kArgReg32[i] << "\n";
        }

        emitStmt(*fn.body);

        // Fallthrough path for a function whose control flow doesn't end in an
        // explicit `return` (e.g. `void`-style functions, or a caller ignoring
        // that a branch is missing one) -- exits with 0 rather than whatever
        // garbage was left in eax.
        out_ << "    mov eax, 0\n";
        out_ << "    leave\n";
        out_ << "    ret\n";
    }

    void emitStmt(const Stmt& stmt) {
        switch (stmt.kind) {
            case Stmt::Kind::VarDecl: {
                auto& d = static_cast<const VarDeclStmt&>(stmt);
                if (d.init) {
                    emitExpr(*d.init);
                } else {
                    out_ << "    mov eax, 0\n";  // deliberately zero-initialized; real C leaves this undefined
                }
                out_ << "    mov " << slot(d.slotOffset) << ", eax\n";
                return;
            }
            case Stmt::Kind::ExprStmt:
                emitExpr(*static_cast<const ExprStmt&>(stmt).expr);
                return;
            case Stmt::Kind::Return: {
                auto& r = static_cast<const ReturnStmt&>(stmt);
                if (r.value) {
                    emitExpr(*r.value);
                } else {
                    out_ << "    mov eax, 0\n";
                }
                out_ << "    leave\n    ret\n";
                return;
            }
            case Stmt::Kind::Block:
                for (auto& item : static_cast<const BlockStmt&>(stmt).items) emitStmt(*item);
                return;
            case Stmt::Kind::If: {
                auto& s = static_cast<const IfStmt&>(stmt);
                std::string lelse = newLabel("Lelse");
                emitExpr(*s.cond);
                out_ << "    cmp eax, 0\n    je " << lelse << "\n";
                emitStmt(*s.thenBranch);
                if (s.elseBranch) {
                    std::string lend = newLabel("Lend");
                    out_ << "    jmp " << lend << "\n" << lelse << ":\n";
                    emitStmt(*s.elseBranch);
                    out_ << lend << ":\n";
                } else {
                    out_ << lelse << ":\n";
                }
                return;
            }
            case Stmt::Kind::While: {
                auto& s = static_cast<const WhileStmt&>(stmt);
                std::string lstart = newLabel("Lwhile");
                std::string lend = newLabel("Lwhileend");
                out_ << lstart << ":\n";
                emitExpr(*s.cond);
                out_ << "    cmp eax, 0\n    je " << lend << "\n";
                loopStack_.push_back({lstart, lend});
                emitStmt(*s.body);
                loopStack_.pop_back();
                out_ << "    jmp " << lstart << "\n" << lend << ":\n";
                return;
            }
            case Stmt::Kind::For: {
                auto& s = static_cast<const ForStmt&>(stmt);
                std::string lstart = newLabel("Lfor");
                std::string lpost = newLabel("Lforpost");
                std::string lend = newLabel("Lforend");
                if (s.init) emitStmt(*s.init);
                out_ << lstart << ":\n";
                if (s.cond) {
                    emitExpr(*s.cond);
                    out_ << "    cmp eax, 0\n    je " << lend << "\n";
                }
                loopStack_.push_back({lpost, lend});  // `continue` runs the post-expression, not the condition
                emitStmt(*s.body);
                loopStack_.pop_back();
                out_ << lpost << ":\n";
                if (s.post) emitExpr(*s.post);
                out_ << "    jmp " << lstart << "\n" << lend << ":\n";
                return;
            }
            case Stmt::Kind::Break:
                out_ << "    jmp " << loopStack_.back().second << "\n";
                return;
            case Stmt::Kind::Continue:
                out_ << "    jmp " << loopStack_.back().first << "\n";
                return;
            case Stmt::Kind::Empty:
                return;
        }
    }

    void emitExpr(const Expr& expr) {
        switch (expr.kind) {
            case Expr::Kind::IntLit:
                out_ << "    mov eax, " << static_cast<const IntLitExpr&>(expr).value << "\n";
                return;
            case Expr::Kind::VarRef:
                out_ << "    mov eax, " << slot(expr.slotOffset) << "\n";
                return;
            case Expr::Kind::Assign: {
                auto& a = static_cast<const AssignExpr&>(expr);
                emitExpr(*a.value);
                out_ << "    mov " << slot(expr.slotOffset) << ", eax\n";
                return;
            }
            case Expr::Kind::Unary:
                emitUnary(static_cast<const UnaryExpr&>(expr));
                return;
            case Expr::Kind::Binary:
                emitBinary(static_cast<const BinaryExpr&>(expr));
                return;
            case Expr::Kind::Logical:
                emitLogical(static_cast<const LogicalExpr&>(expr));
                return;
            case Expr::Kind::Call:
                emitCall(static_cast<const CallExpr&>(expr));
                return;
        }
    }

    void emitUnary(const UnaryExpr& u) {
        emitExpr(*u.operand);
        switch (u.op) {
            case UnaryOp::Neg:
                out_ << "    neg eax\n";
                return;
            case UnaryOp::BitNot:
                out_ << "    not eax\n";
                return;
            case UnaryOp::Not:
                out_ << "    cmp eax, 0\n    sete al\n    movzx eax, al\n";
                return;
        }
    }

    // Evaluates lhs then rhs, leaving lhs in ecx and rhs in eax (via the shared
    // expression stack), which is what every binary-op case below expects.
    void emitOperandsToEcxEax(const BinaryExpr& b) {
        emitExpr(*b.lhs);
        push("rax");
        emitExpr(*b.rhs);
        pop("rcx");
    }

    void emitBinary(const BinaryExpr& b) {
        emitOperandsToEcxEax(b);
        switch (b.op) {
            case BinaryOp::Add:
                out_ << "    add eax, ecx\n";
                return;
            case BinaryOp::Sub:
                out_ << "    sub ecx, eax\n    mov eax, ecx\n";
                return;
            case BinaryOp::Mul:
                out_ << "    imul eax, ecx\n";
                return;
            case BinaryOp::Div:
                out_ << "    mov r11d, eax\n    mov eax, ecx\n    cdq\n    idiv r11d\n";
                return;
            case BinaryOp::Mod:
                out_ << "    mov r11d, eax\n    mov eax, ecx\n    cdq\n    idiv r11d\n    mov eax, edx\n";
                return;
            case BinaryOp::BitAnd:
                out_ << "    and eax, ecx\n";
                return;
            case BinaryOp::BitOr:
                out_ << "    or eax, ecx\n";
                return;
            case BinaryOp::BitXor:
                out_ << "    xor eax, ecx\n";
                return;
            case BinaryOp::Eq:
            case BinaryOp::Ne:
            case BinaryOp::Lt:
            case BinaryOp::Le:
            case BinaryOp::Gt:
            case BinaryOp::Ge: {
                // cmp ecx, eax computes (lhs - rhs), so the flags below read as "lhs <op> rhs".
                out_ << "    cmp ecx, eax\n    " << setForCompare(b.op) << " al\n    movzx eax, al\n";
                return;
            }
        }
    }

    static const char* setForCompare(BinaryOp op) {
        switch (op) {
            case BinaryOp::Eq: return "sete";
            case BinaryOp::Ne: return "setne";
            case BinaryOp::Lt: return "setl";
            case BinaryOp::Le: return "setle";
            case BinaryOp::Gt: return "setg";
            case BinaryOp::Ge: return "setge";
            default: throw std::logic_error("setForCompare: not a comparison");
        }
    }

    void emitLogical(const LogicalExpr& l) {
        std::string lshort = newLabel(l.op == LogicalOp::And ? "Land_false" : "Lor_true");
        std::string lend = newLabel("Llogic_end");
        emitExpr(*l.lhs);
        out_ << "    cmp eax, 0\n    " << (l.op == LogicalOp::And ? "je " : "jne ") << lshort << "\n";
        emitExpr(*l.rhs);
        out_ << "    cmp eax, 0\n    " << (l.op == LogicalOp::And ? "je " : "jne ") << lshort << "\n";
        out_ << "    mov eax, " << (l.op == LogicalOp::And ? 1 : 0) << "\n";
        out_ << "    jmp " << lend << "\n";
        out_ << lshort << ":\n    mov eax, " << (l.op == LogicalOp::And ? 0 : 1) << "\n";
        out_ << lend << ":\n";
    }

    void emitCall(const CallExpr& c) {
        for (auto& arg : c.args) {
            emitExpr(*arg);
            push("rax");
        }
        for (int i = static_cast<int>(c.args.size()) - 1; i >= 0; i--) {
            pop(kArgReg64[i]);
        }
        // The SysV ABI requires rsp to be 16-byte aligned at the `call` instruction.
        // Frames start 16-aligned (see emitFunction) and every push/pop through the
        // helpers above keeps tempDepth_ an exact count of 8-byte slots pushed since
        // then, so an odd count means we're currently 8 bytes off -- pad it away.
        bool pad = (tempDepth_ % 2) != 0;
        if (pad) out_ << "    sub rsp, 8\n";
        out_ << "    call " << c.callee << "\n";
        if (pad) out_ << "    add rsp, 8\n";
    }
};

}  // namespace

void generateCode(const Program& program, std::ostream& out) {
    Codegen cg(out);
    cg.emitProgram(program);
}
