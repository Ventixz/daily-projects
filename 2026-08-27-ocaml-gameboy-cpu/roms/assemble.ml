(* There's no assembler for this toy CPU (and pulling one in would defeat
   the point of hand-rolling the emulator), so this is a tiny two-pass
   assembler just for the one demo program below: labels are resolved to
   JR's signed 8-bit displacement or CALL's absolute address instead of
   being hand-counted, which is the part actually worth automating -- a
   single off-by-one in a byte count is exactly the kind of bug that's
   invisible until the CPU jumps into the middle of an instruction. *)

type item =
  | B of int (* one raw byte *)
  | Lbl of string (* marks the current address *)
  | Jr of int * string (* opcode (0x18/0x20/.../0x38) + relative target *)
  | Call of string (* CALL nn to an absolute target *)

let prog =
  [
    Lbl "start";
    B 0x06; B 0x00; (* LD B,0x00        -- fib(n-2) *)
    B 0x0E; B 0x01; (* LD C,0x01        -- fib(n-1) *)
    B 0x16; B 0x0C; (* LD D,12          -- how many terms to print *)
    Lbl "loop";
    B 0x79; (* LD A,C *)
    Call "print_hex_byte";
    B 0x3E; B 0x20; (* LD A,' ' *)
    B 0xEA; B 0x01; B 0xFF; (* LD (0xFF01),A *)
    B 0x3E; B 0x81; (* LD A,0x81 *)
    B 0xEA; B 0x02; B 0xFF; (* LD (0xFF02),A -- serial "transmit" *)
    B 0x78; (* LD A,B *)
    B 0x81; (* ADD A,C           -- A = next term *)
    B 0x5F; (* LD E,A *)
    B 0x79; (* LD A,C *)
    B 0x47; (* LD B,A            -- B = old C *)
    B 0x7B; (* LD A,E *)
    B 0x4F; (* LD C,A            -- C = next *)
    B 0x15; (* DEC D *)
    Jr (0x20, "loop"); (* JR NZ,loop *)
    B 0x3E; B 0x0A; (* LD A,'\n' *)
    B 0xEA; B 0x01; B 0xFF;
    B 0x3E; B 0x81;
    B 0xEA; B 0x02; B 0xFF;
    B 0x76; (* HALT *)
    Lbl "print_hex_byte";
    (* Prints A as two hex ASCII digits. Only clobbers A and H, so the
       caller's loop state in B/C/D survives the call untouched. *)
    B 0x67; (* LD H,A            -- stash the original byte *)
    B 0x7C; (* LD A,H *)
    B 0xCB; B 0x37; (* SWAP A            -- high nibble into the low bits *)
    B 0xE6; B 0x0F; (* AND 0x0F *)
    Call "print_nibble";
    B 0x7C; (* LD A,H *)
    B 0xE6; B 0x0F; (* AND 0x0F          -- low nibble *)
    Call "print_nibble";
    B 0xC9; (* RET *)
    Lbl "print_nibble";
    B 0xFE; B 0x0A; (* CP 10 *)
    Jr (0x38, "digit"); (* JR C,digit *)
    B 0xC6; B 0x37; (* ADD A,'A'-10 *)
    Jr (0x18, "nibble_out"); (* JR nibble_out *)
    Lbl "digit";
    B 0xC6; B 0x30; (* ADD A,'0' *)
    Lbl "nibble_out";
    B 0xEA; B 0x01; B 0xFF; (* LD (0xFF01),A *)
    B 0x3E; B 0x81;
    B 0xEA; B 0x02; B 0xFF;
    B 0xC9; (* RET *)
  ]

let () =
  let addr = Hashtbl.create 16 in
  let pc = ref 0 in
  List.iter
    (function
      | B _ -> incr pc
      | Lbl name -> Hashtbl.replace addr name !pc
      | Jr _ -> pc := !pc + 2
      | Call _ -> pc := !pc + 3)
    prog;
  let target name =
    match Hashtbl.find_opt addr name with
    | Some a -> a
    | None -> failwith ("undefined label: " ^ name)
  in
  let out = Buffer.create 256 in
  let pc = ref 0 in
  List.iter
    (function
      | B v ->
        Buffer.add_char out (Char.chr (v land 0xFF));
        incr pc
      | Lbl _ -> ()
      | Jr (opcode, label) ->
        let next_pc = !pc + 2 in
        let off = target label - next_pc in
        if off < -128 || off > 127 then failwith ("JR out of range: " ^ label);
        Buffer.add_char out (Char.chr opcode);
        Buffer.add_char out (Char.chr (off land 0xFF));
        pc := next_pc
      | Call label ->
        let a = target label in
        Buffer.add_char out '\xCD';
        Buffer.add_char out (Char.chr (a land 0xFF));
        Buffer.add_char out (Char.chr ((a lsr 8) land 0xFF));
        pc := !pc + 3)
    prog;
  let oc = open_out_bin "roms/fib.gb" in
  output_string oc (Buffer.contents out);
  close_out oc;
  Printf.printf "wrote roms/fib.gb (%d bytes)\n" (Buffer.length out)
