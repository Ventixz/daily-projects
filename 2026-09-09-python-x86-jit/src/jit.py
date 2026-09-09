"""Wires the hand-encoded machine code from codegen.py into something
callable from Python: allocate a page of memory marked executable,
copy the bytes in, and wrap the address in a ctypes function pointer.
"""

import ctypes
import mmap

from .parser import parse
from .codegen import compile_to_machine_code

NUM_SLOTS = 26  # one per lowercase letter, a=0 .. z=25
_FUNC_TYPE = ctypes.CFUNCTYPE(ctypes.c_int64, ctypes.POINTER(ctypes.c_int64))


class JitFunction:
    """A compiled expression. Keeps the mmap'd page alive for as long as
    the function object exists -- if the mmap were garbage collected first,
    `call` would jump into freed (and possibly unmapped) memory."""

    def __init__(self, machine_code):
        self.machine_code = machine_code
        self._buf = mmap.mmap(
            -1,
            len(machine_code),
            prot=mmap.PROT_READ | mmap.PROT_WRITE | mmap.PROT_EXEC,
        )
        self._buf.write(machine_code)
        address = ctypes.addressof(ctypes.c_char.from_buffer(self._buf))
        self._entry = _FUNC_TYPE(address)

    def call(self, variables=None):
        variables = variables or {}
        slots = (ctypes.c_int64 * NUM_SLOTS)()
        for name, value in variables.items():
            if len(name) != 1 or not name.islower():
                raise ValueError(f"variables must be a single lowercase letter, got {name!r}")
            slots[ord(name) - ord("a")] = value
        return self._entry(slots)

    def __call__(self, variables=None):
        return self.call(variables)


def compile_expr(text):
    """Parse `text` and JIT-compile it into a callable JitFunction."""
    ast = parse(text)
    machine_code = compile_to_machine_code(ast)
    return JitFunction(machine_code)
