"""A tree-walking-free, *bytecode*-walking interpreter for a subset of
CPython 3.11 bytecode.

The approach (and the name of the game) comes from the "500 Lines or Less"
essay "A Python Interpreter Written in Python" (byterun): don't parse
source at all, let the real `compile()` builtin do that, and instead
write a virtual machine that executes the resulting `code` object's
bytecode instruction by instruction, maintaining our own value stack and
call frames instead of CPython's.

Scope: see LEARNING.md for exactly what is and isn't supported. Short
version: functions, closures-free scoping, control flow, the built-in
container types, and calls to real Python builtins all work; classes,
generators, closures, exceptions, and `*args`/`**kwargs` do not.
"""

from __future__ import annotations

import builtins
import dis
import operator
import types
from typing import Any


class VMError(Exception):
    """Raised for anything the VM can't or won't execute.

    Deliberately never caught internally -- an unsupported opcode is a
    scope boundary, not a runtime condition a hosted program can react
    to.
    """


class VMRuntimeError(Exception):
    """Wraps a genuine error in the *hosted* program (bad call, missing
    name, wrong argument count) so it reads like the ordinary Python
    exception it stands in for, without pretending to be one."""


# Sentinel pushed by PUSH_NULL / LOAD_GLOBAL's "push a NULL first" bit.
# Real CPython overloads its actual NULL pointer for this; we can't do
# that in pure Python, so we use a private, un-mistakable sentinel object.
NULL = object()


# BINARY_OP's `argrepr` is the operator's source spelling (e.g. "+",
# "+=", "//"). Both the plain and augmented spelling map to the same
# underlying operator call -- CPython emits a distinct assignment
# afterwards (STORE_FAST/STORE_NAME) either way, so BINARY_OP itself
# only ever needs to know which binary operation to apply.
_BINARY_OPS = {
    "+": operator.add, "+=": operator.iadd,
    "-": operator.sub, "-=": operator.isub,
    "*": operator.mul, "*=": operator.imul,
    "/": operator.truediv, "/=": operator.itruediv,
    "//": operator.floordiv, "//=": operator.ifloordiv,
    "%": operator.mod, "%=": operator.imod,
    "**": operator.pow, "**=": operator.ipow,
    "<<": operator.lshift, "<<=": operator.ilshift,
    ">>": operator.rshift, ">>=": operator.irshift,
    "&": operator.and_, "&=": operator.iand,
    "|": operator.or_, "|=": operator.ior,
    "^": operator.xor, "^=": operator.ixor,
    "@": operator.matmul, "@=": operator.imatmul,
}

_COMPARE_OPS = {
    "<": operator.lt, "<=": operator.le,
    "==": operator.eq, "!=": operator.ne,
    ">": operator.gt, ">=": operator.ge,
}


class Function:
    """Our stand-in for a user-defined function object.

    A real `types.FunctionType` is never created -- calling one would
    hand execution back to *real* CPython, defeating the point. This
    just bundles the pieces MAKE_FUNCTION assembles (the code object,
    the defaults, the defining module's globals) so `CALL` can build a
    Frame from them and hand it back to the VM's own dispatch loop.
    """

    def __init__(self, code: types.CodeType, defaults: tuple, global_ns: dict):
        self.code = code
        self.defaults = defaults
        self.global_ns = global_ns
        self.name = code.co_name

    def __repr__(self) -> str:
        return f"<function {self.name} (byterun)>"


class Frame:
    """One call's worth of execution state: the instruction stream, the
    value stack, and the namespaces LOAD_*/STORE_* read and write.

    Real CPython gives function locals a flat array indexed by
    `co_varnames`; we use a plain dict keyed by name instead. It's
    slower and that's fine -- this VM's job is to make LOAD_FAST and
    STORE_FAST's *behavior* legible, not to match CPython's memory
    layout.
    """

    def __init__(self, code: types.CodeType, global_ns: dict, local_ns: dict):
        self.code = code
        self.global_ns = global_ns
        self.local_ns = local_ns
        self.builtin_ns = builtins.__dict__

        self.stack: list[Any] = []
        self.instructions = list(dis.get_instructions(code))
        self.offset_to_index = {
            instr.offset: i for i, instr in enumerate(self.instructions)
        }
        self.pointer = 0

        # Stashed by KW_NAMES for the CALL that immediately follows it.
        self.pending_kw_names: tuple[str, ...] | None = None

    def push(self, value: Any) -> None:
        self.stack.append(value)

    def pop(self) -> Any:
        return self.stack.pop()

    def pop_n(self, n: int) -> list[Any]:
        """Pop `n` values off, returning them in the order they were
        pushed (oldest first) rather than LIFO pop order."""
        if n == 0:
            return []
        values = self.stack[-n:]
        del self.stack[-n:]
        return values

    def jump_to(self, offset: int) -> None:
        self.pointer = self.offset_to_index[offset]


class VirtualMachine:
    """Executes one `code` object at a time by walking its instructions
    and manipulating a `Frame`'s value stack -- no exception handling,
    no generators, no `*args`: see LEARNING.md's scope cuts."""

    def run(self, code: types.CodeType, global_ns: dict | None = None) -> dict:
        """Execute a module-level code object (from
        `compile(src, name, "exec")`). Returns the namespace it ran in."""
        if global_ns is None:
            global_ns = {}
        frame = Frame(code, global_ns, global_ns)
        self.run_frame(frame)
        return global_ns

    def call_function(self, func: Function, args: list, kwargs: dict) -> Any:
        """Bind `args`/`kwargs` to `func`'s parameters the way a plain
        call (no *args/**kwargs on either side) would, then run it."""
        code = func.code
        argcount = code.co_argcount
        param_names = code.co_varnames[:argcount]

        if len(args) > argcount:
            raise VMRuntimeError(
                f"{func.name}() takes {argcount} positional arguments "
                f"but {len(args)} were given"
            )

        local_ns: dict[str, Any] = dict(zip(param_names, args))

        remaining_kwargs = dict(kwargs)
        for name in param_names[len(args):]:
            if name in remaining_kwargs:
                local_ns[name] = remaining_kwargs.pop(name)

        if remaining_kwargs:
            bad = next(iter(remaining_kwargs))
            raise VMRuntimeError(
                f"{func.name}() got an unexpected keyword argument '{bad}'"
            )

        if func.defaults:
            defaulted_names = param_names[argcount - len(func.defaults):]
            for name, default in zip(defaulted_names, func.defaults):
                local_ns.setdefault(name, default)

        missing = [n for n in param_names if n not in local_ns]
        if missing:
            raise VMRuntimeError(
                f"{func.name}() missing required argument(s): "
                f"{', '.join(missing)}"
            )

        frame = Frame(code, func.global_ns, local_ns)
        return self.run_frame(frame)

    def run_frame(self, frame: Frame) -> Any:
        while frame.pointer < len(frame.instructions):
            instr = frame.instructions[frame.pointer]
            frame.pointer += 1

            handler = getattr(self, f"op_{instr.opname}", None)
            if handler is None:
                raise VMError(
                    f"Unsupported opcode: {instr.opname} "
                    f"(offset {instr.offset}) -- see LEARNING.md's scope cuts"
                )

            result = handler(frame, instr)
            if result is not None:
                return_value, = result
                return return_value
        raise VMError("code fell off the end without a RETURN_VALUE")

    # -- no-ops -----------------------------------------------------

    def op_RESUME(self, frame, instr):
        pass

    def op_NOP(self, frame, instr):
        pass

    def op_PRECALL(self, frame, instr):
        pass  # a specialization hint for real CPython; irrelevant here

    # -- stack shuffling ----------------------------------------------

    def op_POP_TOP(self, frame, instr):
        frame.pop()

    def op_PUSH_NULL(self, frame, instr):
        frame.push(NULL)

    def op_COPY(self, frame, instr):
        frame.push(frame.stack[-instr.argval])

    def op_SWAP(self, frame, instr):
        i = instr.argval
        frame.stack[-1], frame.stack[-i] = frame.stack[-i], frame.stack[-1]

    # -- loading and storing --------------------------------------------

    def op_LOAD_CONST(self, frame, instr):
        frame.push(instr.argval)

    def op_LOAD_NAME(self, frame, instr):
        frame.push(self._load_name(frame, instr.argval))

    def op_LOAD_FAST(self, frame, instr):
        name = instr.argval
        if name not in frame.local_ns:
            raise VMRuntimeError(f"local variable '{name}' referenced before assignment")
        frame.push(frame.local_ns[name])

    def op_LOAD_GLOBAL(self, frame, instr):
        if instr.arg & 1:
            frame.push(NULL)
        frame.push(self._load_name(frame, instr.argval, globals_only=True))

    def _load_name(self, frame, name, globals_only=False):
        if not globals_only and name in frame.local_ns:
            return frame.local_ns[name]
        if name in frame.global_ns:
            return frame.global_ns[name]
        if name in frame.builtin_ns:
            return frame.builtin_ns[name]
        raise VMRuntimeError(f"name '{name}' is not defined")

    def op_STORE_NAME(self, frame, instr):
        frame.local_ns[instr.argval] = frame.pop()

    def op_STORE_FAST(self, frame, instr):
        frame.local_ns[instr.argval] = frame.pop()

    def op_STORE_GLOBAL(self, frame, instr):
        frame.global_ns[instr.argval] = frame.pop()

    # -- operators --------------------------------------------------------

    def op_UNARY_NOT(self, frame, instr):
        frame.push(not frame.pop())

    def op_UNARY_NEGATIVE(self, frame, instr):
        frame.push(operator.neg(frame.pop()))

    def op_UNARY_POSITIVE(self, frame, instr):
        frame.push(operator.pos(frame.pop()))

    def op_UNARY_INVERT(self, frame, instr):
        frame.push(operator.invert(frame.pop()))

    def op_BINARY_OP(self, frame, instr):
        rhs = frame.pop()
        lhs = frame.pop()
        op = _BINARY_OPS.get(instr.argrepr)
        if op is None:
            raise VMError(f"Unsupported BINARY_OP spelling: {instr.argrepr!r}")
        frame.push(op(lhs, rhs))

    def op_COMPARE_OP(self, frame, instr):
        rhs = frame.pop()
        lhs = frame.pop()
        op = _COMPARE_OPS.get(instr.argval)
        if op is None:
            raise VMError(f"Unsupported COMPARE_OP: {instr.argval!r}")
        frame.push(op(lhs, rhs))

    def op_IS_OP(self, frame, instr):
        rhs = frame.pop()
        lhs = frame.pop()
        result = lhs is rhs
        frame.push(not result if instr.argval else result)

    def op_CONTAINS_OP(self, frame, instr):
        container = frame.pop()
        needle = frame.pop()
        result = needle in container
        frame.push(not result if instr.argval else result)

    # -- control flow -----------------------------------------------------

    def op_JUMP_FORWARD(self, frame, instr):
        frame.jump_to(instr.argval)

    def op_JUMP_BACKWARD(self, frame, instr):
        frame.jump_to(instr.argval)

    def op_POP_JUMP_FORWARD_IF_FALSE(self, frame, instr):
        if not frame.pop():
            frame.jump_to(instr.argval)

    def op_POP_JUMP_BACKWARD_IF_FALSE(self, frame, instr):
        if not frame.pop():
            frame.jump_to(instr.argval)

    def op_POP_JUMP_FORWARD_IF_TRUE(self, frame, instr):
        if frame.pop():
            frame.jump_to(instr.argval)

    def op_POP_JUMP_BACKWARD_IF_TRUE(self, frame, instr):
        if frame.pop():
            frame.jump_to(instr.argval)

    def op_POP_JUMP_FORWARD_IF_NONE(self, frame, instr):
        if frame.pop() is None:
            frame.jump_to(instr.argval)

    def op_POP_JUMP_BACKWARD_IF_NONE(self, frame, instr):
        if frame.pop() is None:
            frame.jump_to(instr.argval)

    def op_POP_JUMP_FORWARD_IF_NOT_NONE(self, frame, instr):
        if frame.pop() is not None:
            frame.jump_to(instr.argval)

    def op_POP_JUMP_BACKWARD_IF_NOT_NONE(self, frame, instr):
        if frame.pop() is not None:
            frame.jump_to(instr.argval)

    def op_JUMP_IF_FALSE_OR_POP(self, frame, instr):
        if not frame.stack[-1]:
            frame.jump_to(instr.argval)
        else:
            frame.pop()

    def op_JUMP_IF_TRUE_OR_POP(self, frame, instr):
        if frame.stack[-1]:
            frame.jump_to(instr.argval)
        else:
            frame.pop()

    def op_GET_ITER(self, frame, instr):
        frame.push(iter(frame.pop()))

    def op_FOR_ITER(self, frame, instr):
        try:
            frame.push(next(frame.stack[-1]))
        except StopIteration:
            frame.pop()  # the exhausted iterator
            frame.jump_to(instr.argval)

    def op_RETURN_VALUE(self, frame, instr):
        return (frame.pop(),)

    # -- containers ---------------------------------------------------

    def op_BUILD_LIST(self, frame, instr):
        frame.push(frame.pop_n(instr.argval))

    def op_BUILD_TUPLE(self, frame, instr):
        frame.push(tuple(frame.pop_n(instr.argval)))

    def op_BUILD_SET(self, frame, instr):
        frame.push(set(frame.pop_n(instr.argval)))

    def op_BUILD_MAP(self, frame, instr):
        items = frame.pop_n(instr.argval * 2)
        frame.push({items[i]: items[i + 1] for i in range(0, len(items), 2)})

    def op_BUILD_CONST_KEY_MAP(self, frame, instr):
        keys = frame.pop()
        values = frame.pop_n(instr.argval)
        frame.push(dict(zip(keys, values)))

    def op_BUILD_STRING(self, frame, instr):
        frame.push("".join(frame.pop_n(instr.argval)))

    def op_LIST_EXTEND(self, frame, instr):
        # The compiler's constant-folding path for list literals: it
        # emits BUILD_LIST(0), then LOAD_CONST of the whole literal as
        # one pre-built tuple, then LIST_EXTEND to splice it in --
        # rather than one LOAD_CONST/BUILD_LIST pair per element.
        values = frame.pop()
        frame.stack[-instr.argval].extend(values)

    def op_SET_UPDATE(self, frame, instr):
        values = frame.pop()
        frame.stack[-instr.argval].update(values)

    def op_BINARY_SUBSCR(self, frame, instr):
        key = frame.pop()
        container = frame.pop()
        frame.push(container[key])

    def op_STORE_SUBSCR(self, frame, instr):
        key = frame.pop()
        container = frame.pop()
        value = frame.pop()
        container[key] = value

    def op_UNPACK_SEQUENCE(self, frame, instr):
        items = list(frame.pop())
        if len(items) != instr.argval:
            raise VMRuntimeError(
                f"not enough values to unpack (expected {instr.argval}, "
                f"got {len(items)})"
            )
        for value in reversed(items):
            frame.push(value)

    # -- functions and calls ------------------------------------------

    def op_MAKE_FUNCTION(self, frame, instr):
        code = frame.stack[-1]
        if code.co_name in ("<listcomp>", "<dictcomp>", "<setcomp>", "<genexpr>"):
            # Comprehensions compile to an implicitly-called nested code
            # object with its own "self is actually the first argument"
            # calling convention -- out of scope, see LEARNING.md.
            raise VMError(
                "list/dict/set comprehensions and generator expressions "
                "are out of scope -- see LEARNING.md's scope cuts"
            )
        if instr.argval not in (0, 0x01):
            raise VMError(
                "MAKE_FUNCTION with closures/annotations/kwdefaults is out "
                "of scope -- see LEARNING.md's scope cuts"
            )
        code = frame.pop()
        defaults = frame.pop() if instr.argval & 0x01 else ()
        frame.push(Function(code, defaults, frame.global_ns))

    def op_KW_NAMES(self, frame, instr):
        # dis doesn't resolve KW_NAMES's argval to the actual tuple of
        # names (it shows up as the literal string "<unknown>") --
        # co_consts[instr.arg] is the same lookup dis itself just
        # declines to do for this one opcode.
        frame.pending_kw_names = frame.code.co_consts[instr.arg]

    def op_CALL(self, frame, instr):
        argc = instr.argval
        kw_names = frame.pending_kw_names
        frame.pending_kw_names = None

        raw_args = frame.pop_n(argc)
        if kw_names:
            split = argc - len(kw_names)
            positional = raw_args[:split]
            kwargs = dict(zip(kw_names, raw_args[split:]))
        else:
            positional = raw_args
            kwargs = {}

        callable_ = frame.pop()
        self_or_null = frame.pop()
        if self_or_null is not NULL:
            positional = [self_or_null] + positional

        if isinstance(callable_, Function):
            frame.push(self.call_function(callable_, positional, kwargs))
        elif callable(callable_):
            frame.push(callable_(*positional, **kwargs))
        else:
            raise VMRuntimeError(f"'{type(callable_).__name__}' object is not callable")
