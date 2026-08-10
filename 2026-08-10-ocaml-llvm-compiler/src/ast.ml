(* AST for Kal, a small expression language that compiles to LLVM IR. *)

type expr =
  | Number of int
  | Var of string
  | Call of string * expr list
  | Unary of string * expr
  | Binary of string * expr * expr
  | If of expr * expr * expr
  | Let of (string * expr) list * expr
  | For of string * expr * expr * expr option * expr
  (* For (var, start, end_, step_opt, body) *)

type top =
  | Extern of string * string list
  | Def of string * string list * expr

type program = top list
