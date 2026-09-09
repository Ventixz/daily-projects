"""Hand-rolled x86-64 machine code generator.

No assembler, no LLVM -- every opcode and ModRM byte below is written out
by hand. The generated function follows the System V AMD64 calling
convention: it takes one argument, a pointer to a 26-slot array of
int64_t (one slot per lowercase letter, a=0 .. z=25), in RDI, and returns
an int64_t in RAX.

Codegen walks the AST and emits a stack machine: every node leaves its
result pushed on the (real, hardware) stack, so combining two subtrees is
always "generate left, generate right, pop both, combine, push result" --
no register allocator needed because RAX/RBX are reused as scratch space
for every operation and nothing has to stay live across a node boundary.
"""

from .ast_nodes import Num, Var, BinOp, Neg

REX_W = 0x48


def _imm64(n):
    return (n & 0xFFFFFFFFFFFFFFFF).to_bytes(8, "little", signed=False)


def _imm32(n):
    return (n & 0xFFFFFFFF).to_bytes(4, "little", signed=False)


def mov_rax_imm64(n):
    """mov rax, imm64"""
    return bytes([REX_W, 0xB8]) + _imm64(n)


def mov_rax_mem_rdi(disp):
    """mov rax, [rdi + disp32]  (always the disp32 form, even for disp==0 --
    this skips the shorter disp8 encoding real assemblers use as an
    optimization, in exchange for one encoder instead of two)."""
    return bytes([REX_W, 0x8B, 0x87]) + _imm32(disp)


PUSH_RAX = bytes([0x50])
POP_RAX = bytes([0x58])
POP_RBX = bytes([0x5B])
ADD_RAX_RBX = bytes([REX_W, 0x01, 0xD8])
SUB_RAX_RBX = bytes([REX_W, 0x29, 0xD8])
IMUL_RAX_RBX = bytes([REX_W, 0x0F, 0xAF, 0xC3])
CQO = bytes([REX_W, 0x99])
IDIV_RBX = bytes([REX_W, 0xF7, 0xFB])
NEG_RAX = bytes([REX_W, 0xF7, 0xD8])
RET = bytes([0xC3])

_BINOP_CODE = {
    "+": ADD_RAX_RBX,
    "-": SUB_RAX_RBX,
    "*": IMUL_RAX_RBX,
}


def var_slot(name):
    return ord(name) - ord("a")


def emit(node, out):
    """Append the machine code for `node` to bytearray `out`. Every call
    leaves exactly one more value pushed on the stack than when it started."""
    if isinstance(node, Num):
        out += mov_rax_imm64(node.value)
        out += PUSH_RAX
    elif isinstance(node, Var):
        out += mov_rax_mem_rdi(var_slot(node.name) * 8)
        out += PUSH_RAX
    elif isinstance(node, Neg):
        emit(node.operand, out)
        out += POP_RAX
        out += NEG_RAX
        out += PUSH_RAX
    elif isinstance(node, BinOp):
        emit(node.left, out)
        emit(node.right, out)
        out += POP_RBX  # right operand (pushed last, popped first)
        out += POP_RAX  # left operand
        if node.op == "/":
            out += CQO
            out += IDIV_RBX
        else:
            out += _BINOP_CODE[node.op]
        out += PUSH_RAX
    else:
        raise TypeError(f"unknown node type {type(node).__name__}")


def compile_to_machine_code(ast):
    """AST -> bytes: a full function body, including the final pop+ret."""
    out = bytearray()
    emit(ast, out)
    out += POP_RAX
    out += RET
    return bytes(out)
