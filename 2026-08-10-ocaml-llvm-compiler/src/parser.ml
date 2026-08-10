(* Recursive-descent parser with precedence climbing for binary operators,
   in the classic Kaleidoscope-tutorial shape: a table of operator
   precedences, and ParseBinOpRHS folds in operators of equal-or-higher
   precedence before returning control to its caller. *)

open Ast
open Lexer

let binop_prec = function
  | "*" | "/" -> 40
  | "+" | "-" -> 20
  | "<" | ">" | "==" -> 10
  | _ -> -1

type state = { mutable toks : token list }

let peek st = match st.toks with t :: _ -> t | [] -> EOF

let advance st =
  match st.toks with
  | t :: rest -> st.toks <- rest; t
  | [] -> EOF

let expect st tok msg =
  let got = advance st in
  if got <> tok then failwith (Printf.sprintf "expected %s" msg)

let expect_ident st msg =
  match advance st with
  | IDENT s -> s
  | _ -> failwith (Printf.sprintf "expected identifier (%s)" msg)

let rec parse_expr st : expr =
  let lhs = parse_unary st in
  parse_binop_rhs st 0 lhs

and parse_binop_rhs st min_prec lhs =
  match peek st with
  | OP op when binop_prec op >= min_prec ->
    let prec = binop_prec op in
    ignore (advance st);
    let rhs = parse_unary st in
    let rhs =
      match peek st with
      | OP op2 when binop_prec op2 > prec -> parse_binop_rhs st (prec + 1) rhs
      | _ -> rhs
    in
    parse_binop_rhs st min_prec (Binary (op, lhs, rhs))
  | _ -> lhs

and parse_unary st =
  match peek st with
  | OP "-" ->
    ignore (advance st);
    Unary ("-", parse_unary st)
  | _ -> parse_primary st

and parse_primary st =
  match peek st with
  | NUMBER n -> ignore (advance st); Number n
  | LPAREN ->
    ignore (advance st);
    let e = parse_expr st in
    expect st RPAREN ")";
    e
  | IDENT name ->
    ignore (advance st);
    if peek st = LPAREN then begin
      ignore (advance st);
      let args = parse_arg_list st in
      expect st RPAREN ") after call arguments";
      Call (name, args)
    end
    else Var name
  | IF ->
    ignore (advance st);
    let cond = parse_expr st in
    expect st THEN "'then'";
    let then_ = parse_expr st in
    expect st ELSE "'else'";
    let else_ = parse_expr st in
    If (cond, then_, else_)
  | LET ->
    ignore (advance st);
    let bindings = parse_let_bindings st in
    expect st IN "'in'";
    let body = parse_expr st in
    Let (bindings, body)
  | FOR ->
    ignore (advance st);
    let var = expect_ident st "loop variable" in
    expect st ASSIGN "'=' after loop variable";
    let start = parse_expr st in
    expect st COMMA "',' after for start value";
    let end_ = parse_expr st in
    let step =
      if peek st = COMMA then begin
        ignore (advance st);
        Some (parse_expr st)
      end
      else None
    in
    expect st IN "'in' after for header";
    let body = parse_expr st in
    For (var, start, end_, step, body)
  | _ -> failwith "expected an expression"

and parse_arg_list st =
  if peek st = RPAREN then []
  else
    let first = parse_expr st in
    let rec loop acc =
      if peek st = COMMA then begin
        ignore (advance st);
        loop (parse_expr st :: acc)
      end
      else List.rev acc
    in
    loop [ first ]

and parse_let_bindings st =
  let name = expect_ident st "let binding name" in
  expect st ASSIGN "'=' in let binding";
  let value = parse_expr st in
  let first = (name, value) in
  if peek st = COMMA then begin
    ignore (advance st);
    first :: parse_let_bindings st
  end
  else [ first ]

let parse_param_list st =
  if peek st = RPAREN then []
  else
    let rec loop acc =
      let name = expect_ident st "parameter name" in
      if peek st = COMMA then begin
        ignore (advance st);
        loop (name :: acc)
      end
      else List.rev (name :: acc)
    in
    loop []

let parse_top st : top =
  match peek st with
  | EXTERN ->
    ignore (advance st);
    let name = expect_ident st "extern name" in
    expect st LPAREN "'(' after extern name";
    let params = parse_param_list st in
    expect st RPAREN ")";
    expect st SEMI "';' after extern declaration";
    Extern (name, params)
  | DEF ->
    ignore (advance st);
    let name = expect_ident st "function name" in
    expect st LPAREN "'(' after function name";
    let params = parse_param_list st in
    expect st RPAREN ")";
    expect st ASSIGN "'=' before function body";
    let body = parse_expr st in
    expect st SEMI "';' after function body";
    Def (name, params, body)
  | _ -> failwith "expected 'def' or 'extern' at top level"

let parse_program (tokens : token list) : program =
  let st = { toks = tokens } in
  let rec loop acc =
    if peek st = EOF then List.rev acc
    else loop (parse_top st :: acc)
  in
  loop []
