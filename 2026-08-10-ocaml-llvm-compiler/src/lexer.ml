(* Hand-rolled tokenizer. No regex library -- just a cursor over the
   source string, since the token set is small enough that a table-driven
   lexer would be more machinery than the problem needs. *)

type token =
  | DEF
  | EXTERN
  | IF
  | THEN
  | ELSE
  | LET
  | IN
  | FOR
  | IDENT of string
  | NUMBER of int
  | LPAREN
  | RPAREN
  | COMMA
  | SEMI
  | ASSIGN (* single '=' *)
  | OP of string (* + - * / < > == *)
  | EOF

let keyword_of_ident = function
  | "def" -> Some DEF
  | "extern" -> Some EXTERN
  | "if" -> Some IF
  | "then" -> Some THEN
  | "else" -> Some ELSE
  | "let" -> Some LET
  | "in" -> Some IN
  | "for" -> Some FOR
  | _ -> None

let is_digit c = c >= '0' && c <= '9'
let is_alpha c = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c = '_'
let is_alnum c = is_alpha c || is_digit c

(* Returns the full token list for [src], including a trailing EOF. Raises
   [Failure] with a 1-indexed column on any character that doesn't start a
   valid token. *)
let tokenize (src : string) : token list =
  let n = String.length src in
  let pos = ref 0 in
  let tokens = ref [] in
  let push t = tokens := t :: !tokens in
  while !pos < n do
    let c = src.[!pos] in
    if c = ' ' || c = '\t' || c = '\n' || c = '\r' then incr pos
    else if c = '#' then
      (* line comment *)
      while !pos < n && src.[!pos] <> '\n' do
        incr pos
      done
    else if is_digit c then begin
      let start = !pos in
      while !pos < n && is_digit src.[!pos] do
        incr pos
      done;
      push (NUMBER (int_of_string (String.sub src start (!pos - start))))
    end
    else if is_alpha c then begin
      let start = !pos in
      while !pos < n && is_alnum src.[!pos] do
        incr pos
      done;
      let word = String.sub src start (!pos - start) in
      match keyword_of_ident word with
      | Some tok -> push tok
      | None -> push (IDENT word)
    end
    else begin
      match c with
      | '(' -> push LPAREN; incr pos
      | ')' -> push RPAREN; incr pos
      | ',' -> push COMMA; incr pos
      | ';' -> push SEMI; incr pos
      | '=' ->
        if !pos + 1 < n && src.[!pos + 1] = '=' then begin
          push (OP "==");
          pos := !pos + 2
        end else begin
          push ASSIGN;
          incr pos
        end
      | '+' | '-' | '*' | '/' | '<' | '>' ->
        push (OP (String.make 1 c));
        incr pos
      | _ -> failwith (Printf.sprintf "unexpected character %C at column %d" c (!pos + 1))
    end
  done;
  push EOF;
  List.rev !tokens
