(* Flat 64KB address space. Real hardware splits this into ROM banks (via an
   MBC), VRAM, cartridge RAM, work RAM, OAM, I/O registers and HRAM -- all of
   that bank-switching and peripheral behavior is out of scope here (see
   LEARNING.md). Everything below 0x8000 is simply treated as writable
   memory, which is enough for hand-assembled test programs that fit in a
   single 32KB image and never rely on bank switching. *)

type t = {
  bytes : Bytes.t;
  serial_out : Buffer.t;
}

let size = 0x10000

let create () = { bytes = Bytes.make size '\000'; serial_out = Buffer.create 256 }

let load_rom mem (data : string) =
  let n = min (String.length data) size in
  Bytes.blit_string data 0 mem.bytes 0 n

let read mem addr = Char.code (Bytes.get mem.bytes (addr land 0xFFFF))

(* Writes to the real GB's serial port (SB=0xFF01, SC=0xFF02) are how
   headless test ROMs (e.g. Blargg's) report output without any display:
   the byte in SB is "transmitted" the moment SC's start bit (0x80) is set.
   No real link cable is emulated -- the byte is just captured to a buffer
   and SC's transfer-in-progress bit is cleared, mimicking a transfer that
   completes instantly. *)
let write mem addr v =
  let addr = addr land 0xFFFF in
  let v = v land 0xFF in
  Bytes.set mem.bytes addr (Char.chr v);
  if addr = 0xFF02 && v land 0x80 <> 0 then begin
    Buffer.add_char mem.serial_out (Bytes.get mem.bytes 0xFF01);
    Bytes.set mem.bytes 0xFF02 (Char.chr (v land 0x7F))
  end

let serial_output mem = Buffer.contents mem.serial_out
