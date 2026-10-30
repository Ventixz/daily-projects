# Learning: Calculator in Go (with a WebAssembly entry point)

**Source:** "Go WebAssembly Tutorial - Building a Calculator Tutorial" in the Go section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
The tutorial is built around a browser UI; here the focus is the evaluator itself, written by hand, plus a thin wasm wrapper.

Run: `go run ./cmd/calc "2*(3+4)^2 - sqrt(16)"` (or no args for a prompt). Test: `go test ./...`.
Wasm: `GOOS=js GOARCH=wasm go build -o calc.wasm ./cmd/wasm` (compile-checked only; not run in a browser).

## Layout

- `calc/calc.go` — recursive-descent parser that evaluates as it parses.
- `calc/calc_test.go` — precedence, associativity, unary minus, functions/constants, and error cases.
- `cmd/calc` — CLI / REPL. `cmd/wasm` — exposes `goCalc(expr)` to JavaScript via `syscall/js`.

## What I learned

- **One function per precedence level** is the whole trick of recursive descent: `expr` calls `term` calls `unary` calls
  `power` calls `atom`, so tighter-binding operators are parsed deeper.
- **Associativity comes from loops vs recursion.** `+ - * /` loop (left-associative); `^` recurses on its right side so
  `2^3^2 = 512`. Unary minus sits *above* `^`, so `-2^2 = -4`.
- **Go WASM** needs a build tag (`js && wasm`), `js.FuncOf` for callbacks, and `select {}` to keep `main` alive.
- Keeping parsing in a plain package means it's testable natively and reused unchanged by the wasm build.

## Known limits

- Floats only (no big numbers); no variables, assignment or multi-argument functions.
- The browser page was not built or run, only the wasm compile was verified.
