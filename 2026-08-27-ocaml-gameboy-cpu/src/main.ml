let read_file path =
  let ic = open_in_bin path in
  let n = in_channel_length ic in
  let s = really_input_string ic n in
  close_in ic;
  s

let () =
  if Array.length Sys.argv < 2 then begin
    Printf.eprintf "usage: %s <rom.bin>\n" Sys.argv.(0);
    exit 1
  end;
  let rom = read_file Sys.argv.(1) in
  let mem = Memory.create () in
  Memory.load_rom mem rom;
  let cpu = Cpu.create mem in
  cpu.pc <- 0x0000;
  let steps = Cpu.run_until_halt cpu in
  print_string (Memory.serial_output mem);
  Printf.printf
    "\n[halted after %d instructions] AF=%04x BC=%04x DE=%04x HL=%04x SP=%04x PC=%04x\n"
    steps (Cpu.get_af cpu) (Cpu.get_bc cpu) (Cpu.get_de cpu) (Cpu.get_hl cpu) cpu.sp cpu.pc
