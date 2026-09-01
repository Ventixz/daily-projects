# Learning: A Python Implementation of a Python Bytecode Runner

**Source:** ["A Python Interpreter Written in Python"](https://www.aosabook.org/en/500L/a-python-interpreter-written-in-python.html)
(the byterun essay from *500 Lines or Less*), from the Miscellaneous
subsection of the Python section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).

Picked and built end-to-end in one sitting, so this folder contains the
finished implementation directly at the project root (no separate
`reference/`). Zero external dependencies — standard library only
(`dis`, `operator`, `builtins`).

## What it is

- `src/vm.py` — the whole thing. `VirtualMachine.run(code, globals)`
  takes a `code` object from the real `compile(src, name, "exec")` and
  executes it by walking `dis.get_instructions(code)` and driving a
  `Frame`'s own value stack — CPython's C-level eval loop is never
  reached for the hosted program, only for `compile()` itself and for
  the leaf calls this VM delegates to real builtins.
- `Frame` — one call's instruction list, value stack, and the three
  namespaces `LOAD_NAME`/`LOAD_FAST`/`LOAD_GLOBAL` each read from
  (locals, globals, builtins). Jump targets are resolved once per frame
  into an `offset → instruction-index` map, since `dis` reports jump
  targets as byte offsets, not list positions.
- `Function` — the VM's own stand-in for a function object. `def`
  doesn't create a real `types.FunctionType`; `MAKE_FUNCTION` builds one
  of these instead, and `CALL` dispatches to `VirtualMachine.call_function`,
  which binds positional/keyword/default arguments to a fresh `Frame`
  and recurses into `run_frame` — this is where the interpreter's own
  Python call stack stands in for CPython's C call stack. Recursion in
  the *hosted* program (`factorial`, `fib`) is genuine recursion in
  *this* interpreter.
- `tests/test_vm.py` — 38 tests: one group per opcode family
  (arithmetic, control flow, functions, containers), a
  `TestScopeCuts` group that pins down exactly how each excluded
  feature fails, and `TestAgainstRealCPython`, which runs five small
  programs through both this VM and a real `exec()` and asserts the
  resulting globals agree.
- `demo.py` — fibonacci, recursive factorial, a prime sieve, and a
  histogram, run through the VM instead of through `exec()`.

## Run it

```bash
cd 2026-09-01-python-bytecode-interpreter
python3 -m unittest discover -v      # 38 tests
python3 demo.py
```

Actual output:

```
$ python3 demo.py
--- fibonacci (iterative) ---
0 -> 0
1 -> 1
2 -> 1
3 -> 2
...
9 -> 34

--- factorial (recursive) ---
10! = 3628800

--- primes under 30 (sieve-ish trial division) ---
primes under 30: [2, 3, 5, 7, 11, 13, 17, 19, 23, 29]

--- word-length histogram ---
lengths: {4: 5, 3: 1}
```

## What it actually teaches

- **The compiler already did the hard part; `dis` just hands it to
  you.** Writing a *bytecode* interpreter (as opposed to a tree-walking
  one over an AST) means the lexer, parser, and compiler are never your
  problem — `compile()` produces a flat, already-resolved instruction
  stream where every name lookup, every jump target, every operator has
  been pinned down. The entire job left is: hold a stack, and for each
  instruction, do the small thing its opcode says. That's a genuinely
  different (and much smaller) problem than "implement Python."
- **CPython 3.11's calling convention is `[NULL_or_self, callable,
  args...]`, and it took an actual bytecode dump to pin down, not
  intuition.** `LOAD_GLOBAL`'s low oparg bit means "push a NULL before
  the value" specifically so `CALL` always has a uniform 2-slot prefix
  to pop regardless of whether the callable came from a bare name
  (`NULL` prefix) or an attribute (`self` prefix, for bound-method
  calls). `op_CALL` in `vm.py` pops `argc` args, then the callable, then
  that prefix slot, and only *prepends* the prefix slot to the argument
  list when it isn't the `NULL` sentinel. Getting this backwards is the
  single easiest way to build a bytecode VM that looks right on
  zero-argument calls and silently breaks on everything else.
- **`dis.get_instructions` resolves jump targets and most `argval`s for
  you, except when it doesn't.** Every jump opcode's `argval` is already
  the absolute target *offset* (not a relative delta you'd have to add
  yourself), and `COMPARE_OP`/`BINARY_OP`'s `argval`/`argrepr` are
  already the operator string. `KW_NAMES` is the one opcode `dis`
  declines to resolve — its `argval` prints as the literal string
  `"<unknown>"` — so `op_KW_NAMES` has to do the same `co_consts[arg]`
  lookup `dis` does everywhere else, just manually.
- **List/set/dict *literals* aren't always built the way they look.**
  `[10, 20, 30]` doesn't compile to three `LOAD_CONST`s and a
  `BUILD_LIST(3)` — the compiler constant-folds the whole literal into
  one tuple, emits `BUILD_LIST(0)`, and splices it in with
  `LIST_EXTEND(1)`. `{1, 2, 3}` does the same with `SET_UPDATE`, and
  `{'a': 1, 'b': 2}` (constant keys) compiles to `BUILD_CONST_KEY_MAP`
  instead of pushing key/value pairs individually. A VM that only
  handles `BUILD_LIST(n)` with `n` individually-pushed elements passes
  every test with a non-constant list (`[1, 2, x]`) and fails on the
  boring constant one — which is exactly the order these bugs showed up
  in while building this.
- **A `try`/`except` block is invisible to a bytecode-only VM as long as
  nothing raises.** CPython 3.11 moved exception handling out of the
  bytecode stream entirely and into a side table
  (`code.co_exceptiontable`) that this VM never reads. That means
  `try: x = 1 \n except Exception: x = 2` runs to completion looking
  identical to code with no `try` at all — there's no opcode marking
  "we're in a protected region" to notice. `TestScopeCuts` pins down the
  real failure mode instead: the moment the body actually raises
  (`1 / 0`), the genuine `ZeroDivisionError` from Python's own
  `operator.truediv` propagates straight out of `run_frame`, uncaught,
  because nothing in this VM ever consults the exception table that
  would tell it where to jump.

## Deliberate scope cuts

- **No classes, no attribute access at all.** No `LOAD_ATTR`/`STORE_ATTR`/`LOAD_METHOD`
  support, so no `obj.attr`, no `"x".upper()`, no `list.append(x)`.
  Every example uses builtin *functions* (`len`, `sorted`, `range`) and
  operators (`+` for list concatenation, `[]` for subscripting) instead
  of methods.
- **No closures.** `MAKE_FUNCTION` only handles the "plain defaults"
  flag bit; a nested `def` that captures an enclosing local needs
  `LOAD_DEREF`/`STORE_DEREF`/`MAKE_CELL` and a real `freevars`/`cellvars`
  story, which this VM doesn't implement — `TestScopeCuts` confirms it
  fails loudly (`VMError`) rather than capturing the wrong thing.
- **No comprehensions or generator expressions.** They compile to a
  nested code object invoked through the "self" slot of the calling
  convention rather than the plain `NULL` one, which needs the same
  self/method-call machinery as attribute access. `MAKE_FUNCTION`
  detects `<listcomp>`/`<dictcomp>`/`<setcomp>`/`<genexpr>` code objects
  by name and refuses them explicitly, rather than letting them fail
  confusingly deep inside `CALL`.
- **No `*args`/`**kwargs`, no f-strings, no `raise`/`try`/`except` that
  actually catches, no `import`.** Each is a real, separate chunk of
  opcodes (`BUILD_TUPLE_UNPACK`/`DICT_MERGE`, `FORMAT_VALUE`,
  `RAISE_VARARGS` plus the exception table, `IMPORT_NAME`) this VM's
  dispatch table simply doesn't have handlers for — any program that
  needs them gets a clear `VMError` naming the missing opcode, never a
  silent wrong answer.
- **Locals are a `dict`, not CPython's `co_varnames`-indexed array.**
  Simpler to read, and irrelevant to what this project is actually
  demonstrating (bytecode dispatch and the call/return protocol), at the
  cost of the constant-time slot access real `LOAD_FAST` gets.

## What I'd add next

- **Closures.** Implementing `LOAD_DEREF`/`STORE_DEREF`/`MAKE_CELL`
  properly would be the natural next opcode family, and it's the
  feature most of the other cuts (comprehensions included) are actually
  blocked on.
- **A real exception table interpreter.** Reading `code.co_exceptiontable`
  and using it to redirect control flow to a handler on a raised
  exception, instead of just letting Python's own exception propagate
  through `run_frame`, would make `try`/`except` actually work rather
  than accidentally-look-like-it-works-until-something-raises.
- **`*args`/`**kwargs`.** `call_function`'s argument binding already
  isolates all the positional/keyword logic in one place; extending it
  to gather excess positional args into a tuple and excess keywords into
  a dict is a contained change, not a redesign.
