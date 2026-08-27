(* The Sharp SM83 core used by the DMG (Z80-derived, but not a real Z80: no
   IX/IY, no alternate register set, and it adds STOP/the (HL+)/(HL-) forms).
   Instructions are decoded the way the real opcode map is laid out --
   grouped by shared bit fields -- rather than as 500 individual match
   arms; see LEARNING.md for why that's both shorter and closer to how the
   hardware itself decodes instructions. *)

type t = {
  mutable a : int;
  mutable b : int;
  mutable c : int;
  mutable d : int;
  mutable e : int;
  mutable h : int;
  mutable l : int;
  mutable f : int; (* low nibble always 0; bits 7/6/5/4 = Z/N/H/C *)
  mutable sp : int;
  mutable pc : int;
  mutable ime : bool;
  mutable halted : bool;
  mutable stopped : bool;
  mem : Memory.t;
}

let create mem =
  {
    a = 0; b = 0; c = 0; d = 0; e = 0; h = 0; l = 0; f = 0;
    sp = 0xFFFE; pc = 0x0100;
    ime = false; halted = false; stopped = false;
    mem;
  }

(* ---- flags ---- *)
let flag_z = 0x80
let flag_n = 0x40
let flag_h = 0x20
let flag_c = 0x10

let get_z c = c.f land flag_z <> 0
let get_n c = c.f land flag_n <> 0
let get_h c = c.f land flag_h <> 0
let get_c c = c.f land flag_c <> 0

let set_flags c ~z ~n ~h ~cy =
  c.f <- (if z then flag_z else 0)
         lor (if n then flag_n else 0)
         lor (if h then flag_h else 0)
         lor (if cy then flag_c else 0)

(* set only the flags named; the rest keep their previous value *)
let set_flags_p c ?z ?n ?h ?cy () =
  let pick mask cur = function Some true -> mask | Some false -> 0 | None -> cur land mask in
  c.f <- pick flag_z (c.f) z
         lor pick flag_n (c.f) n
         lor pick flag_h (c.f) h
         lor pick flag_c (c.f) cy

(* ---- 16-bit register pairs ---- *)
let get_bc c = (c.b lsl 8) lor c.c
let set_bc c v = c.b <- (v lsr 8) land 0xFF; c.c <- v land 0xFF
let get_de c = (c.d lsl 8) lor c.e
let set_de c v = c.d <- (v lsr 8) land 0xFF; c.e <- v land 0xFF
let get_hl c = (c.h lsl 8) lor c.l
let set_hl c v = c.h <- (v lsr 8) land 0xFF; c.l <- v land 0xFF
let get_af c = (c.a lsl 8) lor (c.f land 0xF0)
let set_af c v = c.a <- (v lsr 8) land 0xFF; c.f <- v land 0xF0

(* ---- fetch ---- *)
let fetch8 c =
  let v = Memory.read c.mem c.pc in
  c.pc <- (c.pc + 1) land 0xFFFF;
  v

let fetch16 c =
  let lo = fetch8 c in
  let hi = fetch8 c in
  (hi lsl 8) lor lo

let signed8 v = if v >= 0x80 then v - 0x100 else v

(* ---- r8 group: B C D E H L (HL) A ---- *)
let get_r8 c idx =
  match idx with
  | 0 -> c.b | 1 -> c.c | 2 -> c.d | 3 -> c.e
  | 4 -> c.h | 5 -> c.l | 6 -> Memory.read c.mem (get_hl c) | 7 -> c.a
  | _ -> assert false

let set_r8 c idx v =
  match idx with
  | 0 -> c.b <- v | 1 -> c.c <- v | 2 -> c.d <- v | 3 -> c.e <- v
  | 4 -> c.h <- v | 5 -> c.l <- v | 6 -> Memory.write c.mem (get_hl c) v
  | 7 -> c.a <- v
  | _ -> assert false

(* ---- r16 group 1: BC DE HL SP (used by LD rr,nn / INC rr / DEC rr / ADD HL,rr) ---- *)
let get_r16_1 c idx =
  match idx with 0 -> get_bc c | 1 -> get_de c | 2 -> get_hl c | 3 -> c.sp | _ -> assert false

let set_r16_1 c idx v =
  match idx with
  | 0 -> set_bc c v | 1 -> set_de c v | 2 -> set_hl c v | 3 -> c.sp <- v land 0xFFFF
  | _ -> assert false

(* ---- r16 group 2: BC DE HL AF (used by PUSH/POP) ---- *)
let get_r16_2 c idx =
  match idx with 0 -> get_bc c | 1 -> get_de c | 2 -> get_hl c | 3 -> get_af c | _ -> assert false

let set_r16_2 c idx v =
  match idx with
  | 0 -> set_bc c v | 1 -> set_de c v | 2 -> set_hl c v | 3 -> set_af c v
  | _ -> assert false

(* ---- condition codes: NZ Z NC C ---- *)
let test_cc c idx =
  match idx with
  | 0 -> not (get_z c) | 1 -> get_z c | 2 -> not (get_c c) | 3 -> get_c c
  | _ -> assert false

(* ---- stack ---- *)
let push16 c v =
  c.sp <- (c.sp - 1) land 0xFFFF;
  Memory.write c.mem c.sp ((v lsr 8) land 0xFF);
  c.sp <- (c.sp - 1) land 0xFFFF;
  Memory.write c.mem c.sp (v land 0xFF)

let pop16 c =
  let lo = Memory.read c.mem c.sp in
  c.sp <- (c.sp + 1) land 0xFFFF;
  let hi = Memory.read c.mem c.sp in
  c.sp <- (c.sp + 1) land 0xFFFF;
  (hi lsl 8) lor lo

(* ---- 8-bit ALU: op index 0..7 = ADD ADC SUB SBC AND XOR OR CP ---- *)
let do_alu c op n =
  let a = c.a in
  match op with
  | 0 ->
    let r = a + n in
    set_flags c ~z:(r land 0xFF = 0) ~n:false ~h:((a land 0xF) + (n land 0xF) > 0xF) ~cy:(r > 0xFF);
    c.a <- r land 0xFF
  | 1 ->
    let cy_in = if get_c c then 1 else 0 in
    let r = a + n + cy_in in
    set_flags c ~z:(r land 0xFF = 0) ~n:false
      ~h:((a land 0xF) + (n land 0xF) + cy_in > 0xF) ~cy:(r > 0xFF);
    c.a <- r land 0xFF
  | 2 ->
    let r = a - n in
    set_flags c ~z:(r land 0xFF = 0) ~n:true ~h:((a land 0xF) < (n land 0xF)) ~cy:(a < n);
    c.a <- r land 0xFF
  | 3 ->
    let cy_in = if get_c c then 1 else 0 in
    let r = a - n - cy_in in
    set_flags c ~z:(r land 0xFF = 0) ~n:true
      ~h:((a land 0xF) < (n land 0xF) + cy_in) ~cy:(a < n + cy_in);
    c.a <- r land 0xFF
  | 4 ->
    c.a <- a land n;
    set_flags c ~z:(c.a = 0) ~n:false ~h:true ~cy:false
  | 5 ->
    c.a <- a lxor n;
    set_flags c ~z:(c.a = 0) ~n:false ~h:false ~cy:false
  | 6 ->
    c.a <- a lor n;
    set_flags c ~z:(c.a = 0) ~n:false ~h:false ~cy:false
  | 7 ->
    let r = a - n in
    set_flags c ~z:(r land 0xFF = 0) ~n:true ~h:((a land 0xF) < (n land 0xF)) ~cy:(a < n)
  | _ -> assert false

let inc_r8 c idx =
  let v = get_r8 c idx in
  let r = (v + 1) land 0xFF in
  set_r8 c idx r;
  set_flags_p c ~z:(r = 0) ~n:false ~h:((v land 0xF) + 1 > 0xF) ()

let dec_r8 c idx =
  let v = get_r8 c idx in
  let r = (v - 1) land 0xFF in
  set_r8 c idx r;
  set_flags_p c ~z:(r = 0) ~n:true ~h:(v land 0xF = 0) ()

let add_hl c v =
  let hl = get_hl c in
  let r = hl + v in
  set_hl c (r land 0xFFFF);
  set_flags_p c ~n:false ~h:((hl land 0xFFF) + (v land 0xFFF) > 0xFFF) ~cy:(r > 0xFFFF) ()

(* SP +/- signed immediate: flags are computed from an *unsigned* byte
   add against SP's low byte, a documented real-hardware quirk that trips
   up naive ports (ADD SP,e8 and LD HL,SP+e8 share this exact rule). *)
let sp_plus_e8_flags c n =
  let h = (c.sp land 0xF) + (n land 0xF) > 0xF in
  let cy = (c.sp land 0xFF) + n > 0xFF in
  (h, cy)

let rlca c =
  let carry = c.a land 0x80 <> 0 in
  c.a <- ((c.a lsl 1) lor (if carry then 1 else 0)) land 0xFF;
  set_flags c ~z:false ~n:false ~h:false ~cy:carry

let rrca c =
  let carry = c.a land 1 <> 0 in
  c.a <- ((c.a lsr 1) lor (if carry then 0x80 else 0)) land 0xFF;
  set_flags c ~z:false ~n:false ~h:false ~cy:carry

let rla c =
  let old_c = get_c c in
  let carry = c.a land 0x80 <> 0 in
  c.a <- ((c.a lsl 1) lor (if old_c then 1 else 0)) land 0xFF;
  set_flags c ~z:false ~n:false ~h:false ~cy:carry

let rra c =
  let old_c = get_c c in
  let carry = c.a land 1 <> 0 in
  c.a <- ((c.a lsr 1) lor (if old_c then 0x80 else 0)) land 0xFF;
  set_flags c ~z:false ~n:false ~h:false ~cy:carry

let daa c =
  let a = ref c.a in
  let adjust = ref 0 in
  let carry = ref (get_c c) in
  if not (get_n c) then begin
    if get_h c || !a land 0xF > 9 then adjust := !adjust lor 0x06;
    if !carry || !a > 0x99 then begin adjust := !adjust lor 0x60; carry := true end;
    a := (!a + !adjust) land 0xFF
  end else begin
    if get_h c then adjust := !adjust lor 0x06;
    if !carry then adjust := !adjust lor 0x60;
    a := (!a - !adjust) land 0xFF
  end;
  c.a <- !a;
  set_flags_p c ~z:(!a = 0) ~h:false ~cy:!carry ()

let cpl c =
  c.a <- (lnot c.a) land 0xFF;
  set_flags_p c ~n:true ~h:true ()

let scf c = set_flags_p c ~n:false ~h:false ~cy:true ()
let ccf c = set_flags_p c ~n:false ~h:false ~cy:(not (get_c c)) ()

(* ---- CB-prefixed rotate/shift group: op index 0..7 =
   RLC RRC RL RR SLA SRA SWAP SRL ---- *)
let do_rot c op v =
  match op with
  | 0 -> let cy = v land 0x80 <> 0 in (((v lsl 1) lor (if cy then 1 else 0)) land 0xFF, cy)
  | 1 -> let cy = v land 1 <> 0 in (((v lsr 1) lor (if cy then 0x80 else 0)) land 0xFF, cy)
  | 2 -> let old_c = get_c c in let cy = v land 0x80 <> 0 in
    (((v lsl 1) lor (if old_c then 1 else 0)) land 0xFF, cy)
  | 3 -> let old_c = get_c c in let cy = v land 1 <> 0 in
    (((v lsr 1) lor (if old_c then 0x80 else 0)) land 0xFF, cy)
  | 4 -> let cy = v land 0x80 <> 0 in ((v lsl 1) land 0xFF, cy)
  | 5 -> let cy = v land 1 <> 0 in (((v lsr 1) lor (v land 0x80)) land 0xFF, cy)
  | 6 -> (((v lsl 4) lor (v lsr 4)) land 0xFF, false)
  | 7 -> let cy = v land 1 <> 0 in ((v lsr 1) land 0xFF, cy)
  | _ -> assert false

let exec_cb c =
  let op2 = fetch8 c in
  let x = (op2 lsr 6) land 3 in
  let y = (op2 lsr 3) land 7 in
  let z = op2 land 7 in
  let wide = if z = 6 then 4 else 2 in
  match x with
  | 0 ->
    let v = get_r8 c z in
    let r, cy = do_rot c y v in
    set_r8 c z r;
    set_flags c ~z:(r = 0) ~n:false ~h:false ~cy;
    wide
  | 1 ->
    let v = get_r8 c z in
    let bit_zero = (v lsr y) land 1 = 0 in
    set_flags_p c ~z:bit_zero ~n:false ~h:true ();
    if z = 6 then 3 else 2
  | 2 ->
    let v = get_r8 c z in
    set_r8 c z (v land (lnot (1 lsl y) land 0xFF));
    wide
  | 3 ->
    let v = get_r8 c z in
    set_r8 c z (v lor (1 lsl y));
    wide
  | _ -> assert false

(* Returns the number of M-cycles (1 M-cycle = 4 T-states) the instruction
   takes, matching the timings in the Pan Docs opcode tables. *)
let step c =
  if c.halted then 1
  else begin
    let op = fetch8 c in
    match op with
    | 0x00 -> 1
    | 0x10 -> ignore (fetch8 c); c.stopped <- true; 1
    | 0x76 -> c.halted <- true; 1
    | 0xF3 -> c.ime <- false; 1
    | 0xFB -> c.ime <- true; 1 (* real hardware delays this by one instruction; moot with no interrupts wired up *)
    | 0x07 -> rlca c; 1
    | 0x0F -> rrca c; 1
    | 0x17 -> rla c; 1
    | 0x1F -> rra c; 1
    | 0x27 -> daa c; 1
    | 0x2F -> cpl c; 1
    | 0x37 -> scf c; 1
    | 0x3F -> ccf c; 1
    | 0x08 ->
      let addr = fetch16 c in
      Memory.write c.mem addr (c.sp land 0xFF);
      Memory.write c.mem (addr + 1) ((c.sp lsr 8) land 0xFF);
      5
    | 0xE8 ->
      let n = fetch8 c in
      let h, cy = sp_plus_e8_flags c n in
      c.sp <- (c.sp + signed8 n) land 0xFFFF;
      set_flags c ~z:false ~n:false ~h ~cy;
      4
    | 0xF8 ->
      let n = fetch8 c in
      let h, cy = sp_plus_e8_flags c n in
      set_hl c ((c.sp + signed8 n) land 0xFFFF);
      set_flags c ~z:false ~n:false ~h ~cy;
      3
    | 0xF9 -> c.sp <- get_hl c; 2
    | 0xC3 -> c.pc <- fetch16 c; 4
    | 0xE9 -> c.pc <- get_hl c; 1
    | 0x18 -> let off = signed8 (fetch8 c) in c.pc <- (c.pc + off) land 0xFFFF; 3
    | 0xCD -> let addr = fetch16 c in push16 c c.pc; c.pc <- addr; 6
    | 0xC9 -> c.pc <- pop16 c; 4
    | 0xD9 -> c.pc <- pop16 c; c.ime <- true; 4
    | 0xE0 -> let n = fetch8 c in Memory.write c.mem (0xFF00 lor n) c.a; 3
    | 0xF0 -> let n = fetch8 c in c.a <- Memory.read c.mem (0xFF00 lor n); 3
    | 0xE2 -> Memory.write c.mem (0xFF00 lor c.c) c.a; 2
    | 0xF2 -> c.a <- Memory.read c.mem (0xFF00 lor c.c); 2
    | 0xEA -> let addr = fetch16 c in Memory.write c.mem addr c.a; 4
    | 0xFA -> let addr = fetch16 c in c.a <- Memory.read c.mem addr; 4
    | 0x02 -> Memory.write c.mem (get_bc c) c.a; 2
    | 0x12 -> Memory.write c.mem (get_de c) c.a; 2
    | 0x0A -> c.a <- Memory.read c.mem (get_bc c); 2
    | 0x1A -> c.a <- Memory.read c.mem (get_de c); 2
    | 0x22 -> Memory.write c.mem (get_hl c) c.a; set_hl c ((get_hl c + 1) land 0xFFFF); 2
    | 0x32 -> Memory.write c.mem (get_hl c) c.a; set_hl c ((get_hl c - 1) land 0xFFFF); 2
    | 0x2A -> c.a <- Memory.read c.mem (get_hl c); set_hl c ((get_hl c + 1) land 0xFFFF); 2
    | 0x3A -> c.a <- Memory.read c.mem (get_hl c); set_hl c ((get_hl c - 1) land 0xFFFF); 2
    | 0xCB -> exec_cb c
    | _ when op land 0xC7 = 0xC7 -> push16 c c.pc; c.pc <- op land 0x38; 4 (* RST *)
    | _ when op land 0xCF = 0xC5 -> push16 c (get_r16_2 c ((op lsr 4) land 3)); 4 (* PUSH rr *)
    | _ when op land 0xCF = 0xC1 -> set_r16_2 c ((op lsr 4) land 3) (pop16 c); 3 (* POP rr *)
    | _ when op land 0xC0 = 0x40 -> (* LD r,r' *)
      let dst = (op lsr 3) land 7 and src = op land 7 in
      set_r8 c dst (get_r8 c src);
      if dst = 6 || src = 6 then 2 else 1
    | _ when op land 0xC7 = 0x06 -> (* LD r,n *)
      let dst = (op lsr 3) land 7 in
      let n = fetch8 c in
      set_r8 c dst n;
      if dst = 6 then 3 else 2
    | _ when op land 0xC0 = 0x80 -> (* ALU A,r *)
      let alu_op = (op lsr 3) land 7 and src = op land 7 in
      do_alu c alu_op (get_r8 c src);
      if src = 6 then 2 else 1
    | _ when op land 0xC7 = 0xC6 -> (* ALU A,n *)
      let alu_op = (op lsr 3) land 7 in
      do_alu c alu_op (fetch8 c);
      2
    | _ when op land 0xC7 = 0x04 -> (* INC r *)
      let dst = (op lsr 3) land 7 in
      inc_r8 c dst;
      if dst = 6 then 3 else 1
    | _ when op land 0xC7 = 0x05 -> (* DEC r *)
      let dst = (op lsr 3) land 7 in
      dec_r8 c dst;
      if dst = 6 then 3 else 1
    | _ when op land 0xCF = 0x01 -> (* LD rr,nn *)
      let grp = (op lsr 4) land 3 in
      set_r16_1 c grp (fetch16 c);
      3
    | _ when op land 0xCF = 0x03 -> (* INC rr *)
      let grp = (op lsr 4) land 3 in
      set_r16_1 c grp ((get_r16_1 c grp + 1) land 0xFFFF);
      2
    | _ when op land 0xCF = 0x0B -> (* DEC rr *)
      let grp = (op lsr 4) land 3 in
      set_r16_1 c grp ((get_r16_1 c grp - 1) land 0xFFFF);
      2
    | _ when op land 0xCF = 0x09 -> (* ADD HL,rr *)
      let grp = (op lsr 4) land 3 in
      add_hl c (get_r16_1 c grp);
      2
    | _ when op land 0xE7 = 0xC2 -> (* JP cc,nn *)
      let cc = (op lsr 3) land 3 in
      let addr = fetch16 c in
      if test_cc c cc then (c.pc <- addr; 4) else 3
    | _ when op land 0xE7 = 0x20 -> (* JR cc,e *)
      let cc = (op lsr 3) land 3 in
      let off = signed8 (fetch8 c) in
      if test_cc c cc then (c.pc <- (c.pc + off) land 0xFFFF; 3) else 2
    | _ when op land 0xE7 = 0xC4 -> (* CALL cc,nn *)
      let cc = (op lsr 3) land 3 in
      let addr = fetch16 c in
      if test_cc c cc then (push16 c c.pc; c.pc <- addr; 6) else 3
    | _ when op land 0xE7 = 0xC0 -> (* RET cc *)
      let cc = (op lsr 3) land 3 in
      if test_cc c cc then (c.pc <- pop16 c; 5) else 2
    | _ -> failwith (Printf.sprintf "illegal opcode 0x%02X at pc=0x%04X" op ((c.pc - 1) land 0xFFFF))
  end

let run_until_halt ?(max_steps = 10_000_000) c =
  let steps = ref 0 in
  while (not c.halted) && (not c.stopped) && !steps < max_steps do
    ignore (step c);
    incr steps
  done;
  !steps
