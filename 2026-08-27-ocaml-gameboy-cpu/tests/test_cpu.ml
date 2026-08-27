(* Minimal hand-rolled test harness -- no opam packages are installed in
   this environment, and the CPU needs nothing beyond assert/printf, so a
   test framework would be a dependency for its own sake. *)

let passed = ref 0
let failed = ref 0

let check name cond =
  if cond then incr passed
  else begin
    incr failed;
    Printf.printf "FAIL: %s\n" name
  end

let check_eq name expected actual =
  check (Printf.sprintf "%s (expected %d, got %d)" name expected actual) (expected = actual)

(* Builds a fresh CPU with `prog` loaded at 0x0000 and PC starting there,
   runs exactly `n` steps (not until HALT, so tests can inspect
   mid-program state or stop right after the instruction under test). *)
let run_n prog n =
  let mem = Memory.create () in
  Memory.load_rom mem (String.init (List.length prog) (fun i -> Char.chr (List.nth prog i)));
  let cpu = Cpu.create mem in
  cpu.pc <- 0x0000;
  cpu.sp <- 0xFFFE;
  for _ = 1 to n do
    ignore (Cpu.step cpu)
  done;
  cpu

let run_until_halt prog =
  let mem = Memory.create () in
  Memory.load_rom mem (String.init (List.length prog) (fun i -> Char.chr (List.nth prog i)));
  let cpu = Cpu.create mem in
  cpu.pc <- 0x0000;
  cpu.sp <- 0xFFFE;
  ignore (Cpu.run_until_halt cpu);
  cpu

(* ---- 8-bit loads ---- *)
let () =
  (* LD B,0x42 ; LD C,B *)
  let c = run_n [ 0x06; 0x42; 0x48 ] 2 in
  check_eq "LD B,n" 0x42 c.b;
  check_eq "LD C,B" 0x42 c.c

let () =
  (* LD HL,0xC000 ; LD (HL),0x99 ; LD A,(HL) *)
  let c = run_n [ 0x21; 0x00; 0xC0; 0x36; 0x99; 0x7E ] 3 in
  check_eq "LD (HL),n then LD A,(HL)" 0x99 c.a

let () =
  (* LD HL,0xC000 ; LD A,0x07 ; LD (HL+),A ; INC A ; LD (HL),A -> HL should now be 0xC001 *)
  let c = run_n [ 0x21; 0x00; 0xC0; 0x3E; 0x07; 0x22 ] 3 in
  check_eq "LD (HL+),A increments HL" 0xC001 (Cpu.get_hl c);
  check_eq "LD (HL+),A wrote A to old HL" 0x07 (Memory.read c.mem 0xC000)

(* ---- 8-bit ALU ---- *)
let () =
  (* LD A,0x0F ; ADD A,0x01 -> half-carry set, result 0x10 *)
  let c = run_n [ 0x3E; 0x0F; 0xC6; 0x01 ] 2 in
  check_eq "ADD A,n result" 0x10 c.a;
  check "ADD A,n sets H on nibble carry" (Cpu.get_h c);
  check "ADD A,n clears Z" (not (Cpu.get_z c))

let () =
  (* LD A,0xFF ; ADD A,0x01 -> zero + carry *)
  let c = run_n [ 0x3E; 0xFF; 0xC6; 0x01 ] 2 in
  check_eq "ADD A,n wraps to 0" 0 c.a;
  check "ADD A,n sets Z on wrap" (Cpu.get_z c);
  check "ADD A,n sets C on overflow" (Cpu.get_c c)

let () =
  (* LD A,0x10 ; SUB A,0x01 -> 0x0F, half-borrow *)
  let c = run_n [ 0x3E; 0x10; 0xD6; 0x01 ] 2 in
  check_eq "SUB A,n result" 0x0F c.a;
  check "SUB A,n sets H on nibble borrow" (Cpu.get_h c);
  check "SUB A,n sets N" (Cpu.get_n c)

let () =
  (* LD A,0xF0 ; CP 0xF0 -> Z set, A unchanged *)
  let c = run_n [ 0x3E; 0xF0; 0xFE; 0xF0 ] 2 in
  check_eq "CP does not modify A" 0xF0 c.a;
  check "CP sets Z on equal" (Cpu.get_z c)

let () =
  (* LD A,0xFF ; AND A,0x0F ; XOR A,0xFF ; OR A,0x01 *)
  let c = run_n [ 0x3E; 0xFF; 0xE6; 0x0F; 0xEE; 0xFF; 0xF6; 0x01 ] 4 in
  check_eq "AND/XOR/OR chain" 0xF1 c.a

let () =
  (* ADC across a carry: LD A,0xFF ; ADD A,0x01 (-> A=0, C=1) ; LD A,0x01 ; ADC A,0x01 -> 0x03 *)
  let c = run_n [ 0x3E; 0xFF; 0xC6; 0x01; 0x3E; 0x01; 0xCE; 0x01 ] 4 in
  check_eq "ADC includes carry-in" 0x03 c.a

(* ---- inc/dec leave carry alone ---- *)
let () =
  (* SCF ; LD A,0x0F ; INC A -> carry must still be set afterwards *)
  let c = run_n [ 0x37; 0x3E; 0x0F; 0x3C ] 3 in
  check_eq "INC r result" 0x10 c.a;
  check "INC r sets H on nibble carry" (Cpu.get_h c);
  check "INC r does not touch C" (Cpu.get_c c)

let () =
  (* LD B,0x01 ; DEC B -> zero *)
  let c = run_n [ 0x06; 0x01; 0x05 ] 2 in
  check_eq "DEC r to zero" 0 c.b;
  check "DEC r sets Z" (Cpu.get_z c);
  check "DEC r sets N" (Cpu.get_n c)

(* ---- 16-bit registers ---- *)
let () =
  (* LD BC,0x1234 ; INC BC *)
  let c = run_n [ 0x01; 0x34; 0x12; 0x03 ] 2 in
  check_eq "LD rr,nn / INC rr" 0x1235 (Cpu.get_bc c)

let () =
  (* LD HL,0x0FFF ; LD BC,0x0001 ; ADD HL,BC -> half-carry across bit 11 *)
  let c = run_n [ 0x21; 0xFF; 0x0F; 0x01; 0x01; 0x00; 0x09 ] 3 in
  check_eq "ADD HL,rr result" 0x1000 (Cpu.get_hl c);
  check "ADD HL,rr sets H" (Cpu.get_h c)

(* ---- stack ---- *)
let () =
  (* LD BC,0xBEEF ; PUSH BC ; POP DE *)
  let c = run_n [ 0x01; 0xEF; 0xBE; 0xC5; 0xD1 ] 3 in
  check_eq "PUSH/POP round-trip" 0xBEEF (Cpu.get_de c);
  check_eq "SP restored after PUSH+POP" 0xFFFE c.sp

(* ---- control flow ---- *)
let () =
  (* JP 0x0006 ; (pad) ; LD A,0x11 at 0x0006 ; HALT *)
  let c =
    run_until_halt [ 0xC3; 0x06; 0x00; 0x00; 0x00; 0x00; 0x3E; 0x11; 0x76 ]
  in
  check_eq "JP nn jumps" 0x11 c.a

let () =
  (* XOR A (Z=1) ; JR Z,+2 ; LD A,0xEE (skipped) ; LD A,0x22 ; HALT *)
  let c = run_until_halt [ 0xAF; 0x28; 0x02; 0x3E; 0xEE; 0x3E; 0x22; 0x76 ] in
  check_eq "JR cc,e takes the branch" 0x22 c.a

let () =
  (* CALL 0x0005 ; HALT ; (at 0x0005) LD A,0x99 ; RET -- returns to the HALT *)
  let c = run_until_halt [ 0xCD; 0x05; 0x00; 0x00; 0x00; 0x3E; 0x99; 0xC9 ] in
  check_eq "CALL/RET round-trip" 0x99 c.a

let () =
  (* RST 0x08 lands at 0x0008: LD A,0x55 ; HALT *)
  let c = run_until_halt [ 0xCF; 0x00; 0x00; 0x00; 0x00; 0x00; 0x00; 0x00; 0x3E; 0x55; 0x76 ] in
  check_eq "RST 08h" 0x55 c.a

(* ---- CB-prefixed ops ---- *)
let () =
  (* LD B,0x80 ; CB RLC B -> 0x01 with carry out *)
  let c = run_n [ 0x06; 0x80; 0xCB; 0x00 ] 2 in
  check_eq "CB RLC r" 0x01 c.b;
  check "CB RLC r sets carry from bit 7" (Cpu.get_c c)

let () =
  (* LD B,0x40 ; CB BIT 6,B -> Z clear ; CB BIT 0,B -> Z set *)
  let c = run_n [ 0x06; 0x40; 0xCB; 0x70 ] 2 in
  check "CB BIT b,r clears Z when bit set" (not (Cpu.get_z c));
  let c2 = run_n [ 0x06; 0x40; 0xCB; 0x70; 0xCB; 0x40 ] 3 in
  check "CB BIT b,r sets Z when bit clear" (Cpu.get_z c2)

let () =
  (* LD B,0xFF ; CB RES 0,B ; CB SET 7,B (already set) *)
  let c = run_n [ 0x06; 0xFF; 0xCB; 0x80; 0xCB; 0xF8 ] 3 in
  check_eq "CB RES/SET b,r" 0xFE c.b

let () =
  (* LD B,0x12 ; CB SWAP B -> 0x21 *)
  let c = run_n [ 0x06; 0x12; 0xCB; 0x30 ] 2 in
  check_eq "CB SWAP r" 0x21 c.b

(* ---- DAA: classic BCD addition 0x15 + 0x27 = 0x42 ---- *)
let () =
  let c = run_n [ 0x3E; 0x15; 0xC6; 0x27; 0x27 ] 3 in
  check_eq "DAA after BCD add" 0x42 c.a

(* ---- SP-relative flags quirk (unsigned-byte add against SP's low byte) ---- *)
let () =
  (* LD SP,0x00FF ; LD HL,SP+1 -> H and C both set (0xFF + 0x01 carries out of both nibbles) *)
  let c = run_n [ 0x31; 0xFF; 0x00; 0xF8; 0x01 ] 2 in
  check_eq "LD HL,SP+e8 result" 0x0100 (Cpu.get_hl c);
  check "LD HL,SP+e8 sets H" (Cpu.get_h c);
  check "LD HL,SP+e8 sets C" (Cpu.get_c c)

(* ---- serial port capture (the Blargg test-ROM convention) ---- *)
let () =
  let mem = Memory.create () in
  let prog =
    (* LD A,'H' ; LD (0xFF01),A ; LD A,0x81 ; LD (0xFF02),A ; HALT *)
    [ 0x3E; 0x48; 0xEA; 0x01; 0xFF; 0x3E; 0x81; 0xEA; 0x02; 0xFF; 0x76 ]
  in
  Memory.load_rom mem (String.init (List.length prog) (fun i -> Char.chr (List.nth prog i)));
  let cpu = Cpu.create mem in
  cpu.pc <- 0x0000;
  ignore (Cpu.run_until_halt cpu);
  check "serial write with SC start bit is captured" (Memory.serial_output mem = "H")

let () =
  Printf.printf "%d passed, %d failed\n" !passed !failed;
  if !failed > 0 then exit 1
