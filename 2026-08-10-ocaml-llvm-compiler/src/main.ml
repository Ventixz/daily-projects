let read_file path =
  let ic = open_in_bin path in
  let n = in_channel_length ic in
  let s = really_input_string ic n in
  close_in ic;
  s

let () =
  if Array.length Sys.argv <> 2 then begin
    prerr_endline "usage: kalc <file.kal>   (LLVM IR is written to stdout)";
    exit 1
  end;
  let src = read_file Sys.argv.(1) in
  match
    let tokens = Lexer.tokenize src in
    let prog = Parser.parse_program tokens in
    Codegen.compile prog
  with
  | ir -> print_string ir
  | exception Failure msg ->
    Printf.eprintf "kalc: %s\n" msg;
    exit 1
