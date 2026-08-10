(* End-to-end tests: for each case, compile Kal source to LLVM IR
   in-process, hand it to clang to produce a native binary, run that
   binary, and check its stdout. This exercises the real pipeline a user
   runs (kalc -> clang -> executable), not just the in-memory AST/IR, so a
   test only passes if the emitted IR is actually valid enough for LLVM to
   accept -- not just "the OCaml code didn't raise." *)

let compile_source src =
  let tokens = Lexer.tokenize src in
  let prog = Parser.parse_program tokens in
  Codegen.compile prog

(* Runs [cmd] (already a full shell command line), returns (exit_code,
   stdout). No Unix dependency needed: Filename.temp_file gives us a
   collision-free scratch file per call and Sys.command shells out. *)
let run_capturing cmd =
  let out_file = Filename.temp_file "kalc_test" ".out" in
  let code = Sys.command (Printf.sprintf "%s > %s 2>%s.err" cmd out_file out_file) in
  let ic = open_in out_file in
  let n = in_channel_length ic in
  let output = really_input_string ic n in
  close_in ic;
  (try Sys.remove out_file with Sys_error _ -> ());
  (try Sys.remove (out_file ^ ".err") with Sys_error _ -> ());
  (code, output)

let passed = ref 0
let failed = ref 0

let fail name reason =
  incr failed;
  Printf.printf "FAIL %s -- %s\n" name reason

let ok name =
  incr passed;
  Printf.printf "ok   %s\n" name

(* Compiles [src], links it against runtime.o, runs it, and checks stdout
   equals [expected]. *)
let expect_output name src expected =
  match compile_source src with
  | exception Failure msg -> fail name (Printf.sprintf "compilation raised: %s" msg)
  | ir -> (
    let ll_file = Filename.temp_file "kalc_test" ".ll" in
    let bin_file = Filename.temp_file "kalc_test" ".bin" in
    let oc = open_out ll_file in
    output_string oc ir;
    close_out oc;
    let clang_code, clang_err =
      run_capturing (Printf.sprintf "clang %s runtime.o -o %s" ll_file bin_file)
    in
    if clang_code <> 0 then
      fail name (Printf.sprintf "clang rejected generated IR (exit %d): %s" clang_code clang_err)
    else begin
      let run_code, output = run_capturing bin_file in
      if output <> expected then
        fail name
          (Printf.sprintf "expected stdout %S, got %S (exit %d)" expected output run_code)
      else ok name
    end;
    (try Sys.remove ll_file with Sys_error _ -> ());
    try Sys.remove bin_file with Sys_error _ -> ())

(* Compiles [src] and asserts it never becomes a runnable binary -- either
   rejected at parse/codegen time (a [Failure] from our own compiler), or,
   for something codegen can't catch on its own (a call to a function
   that's never declared or defined anywhere), rejected when clang tries
   to assemble the resulting IR. Either way, the point under test is
   "this input must not silently produce a program". *)
let expect_rejected name src =
  match compile_source src with
  | exception Failure _ -> ok name
  | ir ->
    let ll_file = Filename.temp_file "kalc_test" ".ll" in
    let bin_file = Filename.temp_file "kalc_test" ".bin" in
    let oc = open_out ll_file in
    output_string oc ir;
    close_out oc;
    let clang_code, _ =
      run_capturing (Printf.sprintf "clang %s runtime.o -o %s" ll_file bin_file)
    in
    (try Sys.remove ll_file with Sys_error _ -> ());
    (try Sys.remove bin_file with Sys_error _ -> ());
    if clang_code <> 0 then ok name
    else fail name (Printf.sprintf "expected compilation to fail, but clang accepted it:\n%s" ir)

let () =
  expect_output "arithmetic precedence"
    "extern print_int(x);\n\
     def main() =\n\
    \  let r = print_int(2 + 3 * 4 - 1) in\n\
    \  0;\n"
    "13\n";

  expect_output "unary minus and parens"
    "extern print_int(x);\n\
     def main() =\n\
    \  let r = print_int(-(3 + 4) * 2) in\n\
    \  0;\n"
    "-14\n";

  expect_output "integer division truncates toward zero"
    "extern print_int(x);\n\
     def main() =\n\
    \  let r = print_int(7 / 2) in\n\
    \  0;\n"
    "3\n";

  expect_output "recursive fibonacci"
    "extern print_int(x);\n\
     def fib(n) = if n < 2 then n else fib(n - 1) + fib(n - 2);\n\
     def main() =\n\
    \  let r = print_int(fib(10)) in\n\
    \  0;\n"
    "55\n";

  expect_output "mutual recursion, forward reference resolves"
    "extern print_int(x);\n\
     def is_even(n) = if n == 0 then 1 else is_odd(n - 1);\n\
     def is_odd(n)  = if n == 0 then 0 else is_even(n - 1);\n\
     def main() =\n\
    \  let a = print_int(is_even(20)) in\n\
    \  let b = print_int(is_odd(20)) in\n\
    \  0;\n"
    "1\n0\n";

  expect_output "nested if in a then-branch (phi predecessor tracking)"
    "extern print_int(x);\n\
     def classify(n) =\n\
    \  if n < 0 then (if n < -10 then 100 else 200)\n\
    \  else n;\n\
     def main() =\n\
    \  let a = print_int(classify(-20)) in\n\
    \  let b = print_int(classify(-5)) in\n\
    \  let c = print_int(classify(5)) in\n\
    \  0;\n"
    "100\n200\n5\n";

  expect_output "for loop, default step 1, half-open range"
    "extern print_int(x);\n\
     def main() =\n\
    \  for i = 1, 6 in print_int(i);\n"
    "1\n2\n3\n4\n5\n";

  expect_output "for loop with explicit step"
    "extern print_int(x);\n\
     def main() =\n\
    \  for i = 0, 10, 2 in print_int(i);\n"
    "0\n2\n4\n6\n8\n";

  expect_output "for loop that never runs (start >= end)"
    "extern print_int(x);\n\
     def main() =\n\
    \  let a = for i = 5, 5 in print_int(i) in\n\
    \  print_int(999);\n"
    "999\n";

  expect_output "nested if inside a for body (latch-block tracking)"
    "extern print_int(x);\n\
     def main() =\n\
    \  for i = 0, 10 in\n\
    \    if i == (i / 2) * 2 then print_int(i) else 0;\n"
    "0\n2\n4\n6\n8\n";

  expect_output "let-chain: later bindings see earlier ones, shadowing works"
    "extern print_int(x);\n\
     def main() =\n\
    \  let x = 1, y = x + 1 in\n\
    \  let x = x + y in\n\
    \  print_int(x);\n"
    "3\n";

  expect_output "comparisons produce usable i32 values, not just branch conditions"
    "extern print_int(x);\n\
     def main() =\n\
    \  print_int((3 < 5) + (5 < 3) + (4 == 4));\n"
    "2\n";

  expect_rejected "parse error: missing 'then'"
    "def main() = if 1 else 2;\n";

  expect_rejected "unbound variable is caught at codegen, not silently zero"
    "extern print_int(x);\n\
     def main() = print_int(y);\n";

  expect_rejected "call to undeclared function"
    "def main() = mystery(1, 2);\n";

  Printf.printf "\n%d passed, %d failed\n" !passed !failed;
  if !failed > 0 then exit 1
