(* Emits LLVM IR as text -- no llvm/opam bindings, just strings. That
   means there's no in-memory instruction we can go back and edit once
   emitted, which is what a real IRBuilder gives you for free. A loop's phi
   node needs the SSA value computed at the *end* of the loop body before it
   can be written, but the phi itself has to appear at the *start* of the
   loop header, textually before that value exists. [Lines.reserve] is the
   workaround: grab a blank slot now, fill it in once the value is known. *)

open Ast

module Lines = struct
  type t = { mutable data : string array; mutable len : int }

  let create () = { data = Array.make 16 ""; len = 0 }

  let ensure_capacity buf n =
    if n > Array.length buf.data then begin
      let bigger = Array.make (max n (2 * Array.length buf.data)) "" in
      Array.blit buf.data 0 bigger 0 buf.len;
      buf.data <- bigger
    end

  let push buf line =
    ensure_capacity buf (buf.len + 1);
    buf.data.(buf.len) <- line;
    let idx = buf.len in
    buf.len <- buf.len + 1;
    idx

  let set buf idx line = buf.data.(idx) <- line

  let contents buf = String.concat "\n" (Array.to_list (Array.sub buf.data 0 buf.len))
end

type fn_state = {
  mutable current_block : string;
  buf : Lines.t;
}

(* Registers and block labels are handed out from module-wide counters, not
   reset per function. Slightly wasteful (register numbers jump between
   functions) but it means "unique across the module" holds trivially --
   no bookkeeping needed to avoid a %r3 in @main colliding with a %r3 in
   @fib. *)
let reg_counter = ref 0
let label_counter = ref 0

let new_reg () =
  incr reg_counter;
  Printf.sprintf "%%r%d" !reg_counter

let new_label prefix =
  incr label_counter;
  Printf.sprintf "%s%d" prefix !label_counter

let emit st line = ignore (Lines.push st.buf ("  " ^ line))

let start_block st label =
  ignore (Lines.push st.buf (label ^ ":"));
  st.current_block <- label

(* Every value in Kal is i32, including the result of a comparison. LLVM's
   icmp produces i1, so comparisons immediately zext back to i32 -- and
   `if`/`for` conditions do the inverse (icmp ne ..., 0) to narrow an i32
   back to the i1 a br needs. Values only ever cross that i1/i32 boundary
   at those two points; nothing else in codegen has to think about it. *)
let gen_comparison st llvm_pred v1 v2 =
  let cmp = new_reg () in
  emit st (Printf.sprintf "%s = icmp %s i32 %s, %s" cmp llvm_pred v1 v2);
  let widened = new_reg () in
  emit st (Printf.sprintf "%s = zext i1 %s to i32" widened cmp);
  widened

let rec gen_expr env st (e : expr) : string =
  match e with
  | Number n -> string_of_int n
  | Var name -> (
    match List.assoc_opt name env with
    | Some v -> v
    | None -> failwith (Printf.sprintf "unbound variable '%s'" name))
  | Unary ("-", e) ->
    let v = gen_expr env st e in
    let r = new_reg () in
    emit st (Printf.sprintf "%s = sub i32 0, %s" r v);
    r
  | Unary (op, _) -> failwith (Printf.sprintf "unknown unary operator '%s'" op)
  | Binary (op, lhs, rhs) ->
    let v1 = gen_expr env st lhs in
    let v2 = gen_expr env st rhs in
    (match op with
     | "+" ->
       let r = new_reg () in
       emit st (Printf.sprintf "%s = add i32 %s, %s" r v1 v2);
       r
     | "-" ->
       let r = new_reg () in
       emit st (Printf.sprintf "%s = sub i32 %s, %s" r v1 v2);
       r
     | "*" ->
       let r = new_reg () in
       emit st (Printf.sprintf "%s = mul i32 %s, %s" r v1 v2);
       r
     | "/" ->
       let r = new_reg () in
       emit st (Printf.sprintf "%s = sdiv i32 %s, %s" r v1 v2);
       r
     | "<" -> gen_comparison st "slt" v1 v2
     | ">" -> gen_comparison st "sgt" v1 v2
     | "==" -> gen_comparison st "eq" v1 v2
     | _ -> failwith (Printf.sprintf "unknown binary operator '%s'" op))
  | Call (name, args) ->
    let vs = List.map (gen_expr env st) args in
    let arglist = String.concat ", " (List.map (fun v -> "i32 " ^ v) vs) in
    let r = new_reg () in
    emit st (Printf.sprintf "%s = call i32 @%s(%s)" r name arglist);
    r
  | If (cond, then_, else_) ->
    let cond_val = gen_expr env st cond in
    let cond_bit = new_reg () in
    emit st (Printf.sprintf "%s = icmp ne i32 %s, 0" cond_bit cond_val);
    let then_lbl = new_label "then" in
    let else_lbl = new_label "else" in
    let cont_lbl = new_label "ifcont" in
    emit st (Printf.sprintf "br i1 %s, label %%%s, label %%%s" cond_bit then_lbl else_lbl);
    start_block st then_lbl;
    let then_val = gen_expr env st then_ in
    (* The block we end in can differ from [then_lbl] if [then_] itself
       contains an if/for (which opens and closes its own blocks) -- the
       phi's predecessor has to be the block that actually falls through
       to ifcont, not the one the branch originally landed in. *)
    let then_end = st.current_block in
    emit st (Printf.sprintf "br label %%%s" cont_lbl);
    start_block st else_lbl;
    let else_val = gen_expr env st else_ in
    let else_end = st.current_block in
    emit st (Printf.sprintf "br label %%%s" cont_lbl);
    start_block st cont_lbl;
    let result = new_reg () in
    emit st
      (Printf.sprintf "%s = phi i32 [ %s, %%%s ], [ %s, %%%s ]" result then_val then_end
         else_val else_end);
    result
  | Let (bindings, body) ->
    let env' =
      List.fold_left (fun env (name, value_expr) -> (name, gen_expr env st value_expr) :: env)
        env bindings
    in
    gen_expr env' st body
  | For (var, start_e, end_e, step_e, body_e) ->
    let start_val = gen_expr env st start_e in
    let end_val = gen_expr env st end_e in
    let step_val = match step_e with Some e -> gen_expr env st e | None -> "1" in
    let preheader = st.current_block in
    let loop_lbl = new_label "loop" in
    emit st (Printf.sprintf "br label %%%s" loop_lbl);
    start_block st loop_lbl;
    (* Reserved now, filled in once next_i and the latch block are known. *)
    let phi_idx = Lines.push st.buf "" in
    let i_reg = new_reg () in
    let cond_bit = new_reg () in
    emit st (Printf.sprintf "%s = icmp slt i32 %s, %s" cond_bit i_reg end_val);
    let body_lbl = new_label "forbody" in
    let after_lbl = new_label "afterfor" in
    emit st (Printf.sprintf "br i1 %s, label %%%s, label %%%s" cond_bit body_lbl after_lbl);
    start_block st body_lbl;
    let loop_env = (var, i_reg) :: env in
    ignore (gen_expr loop_env st body_e);
    let next_i = new_reg () in
    emit st (Printf.sprintf "%s = add i32 %s, %s" next_i i_reg step_val);
    let latch = st.current_block in
    emit st (Printf.sprintf "br label %%%s" loop_lbl);
    Lines.set st.buf phi_idx
      (Printf.sprintf "  %s = phi i32 [ %s, %%%s ], [ %s, %%%s ]" i_reg start_val preheader
         next_i latch);
    start_block st after_lbl;
    "0"

let gen_function name params body =
  let st = { current_block = ""; buf = Lines.create () } in
  let env = List.map (fun p -> (p, "%" ^ p)) params in
  start_block st "entry";
  let result = gen_expr env st body in
  emit st (Printf.sprintf "ret i32 %s" result);
  let param_list = String.concat ", " (List.map (fun p -> "i32 %" ^ p) params) in
  Printf.sprintf "define i32 @%s(%s) {\n%s\n}\n" name param_list (Lines.contents st.buf)

let compile (prog : program) : string =
  let out = Buffer.create 1024 in
  List.iter
    (function
      | Extern (name, params) ->
        let param_types = String.concat ", " (List.map (fun _ -> "i32") params) in
        Buffer.add_string out (Printf.sprintf "declare i32 @%s(%s)\n" name param_types)
      | Def _ -> ())
    prog;
  Buffer.add_char out '\n';
  List.iter
    (function
      | Def (name, params, body) -> Buffer.add_string out (gen_function name params body)
      | Extern _ -> ())
    prog;
  Buffer.contents out
