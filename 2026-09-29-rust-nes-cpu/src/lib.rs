//! A MOS 6502 CPU core (official opcodes subset) as used by the NES.

pub const FLAG_C: u8 = 0b0000_0001;
pub const FLAG_Z: u8 = 0b0000_0010;
pub const FLAG_I: u8 = 0b0000_0100;
pub const FLAG_V: u8 = 0b0100_0000;
pub const FLAG_N: u8 = 0b1000_0000;
const FLAG_B: u8 = 0b0001_0000;
const FLAG_U: u8 = 0b0010_0000;

pub struct Cpu {
    pub a: u8,
    pub x: u8,
    pub y: u8,
    pub sp: u8,
    pub pc: u16,
    pub status: u8,
    mem: Box<[u8; 0x10000]>,
}

#[derive(Clone, Copy)]
enum Mode {
    Imm,
    Zp,
    ZpX,
    ZpY,
    Abs,
    AbsX,
    AbsY,
    IndX,
    IndY,
}

impl Default for Cpu {
    fn default() -> Self {
        Self::new()
    }
}

impl Cpu {
    pub fn new() -> Self {
        Cpu { a: 0, x: 0, y: 0, sp: 0xFD, pc: 0, status: FLAG_I | FLAG_U, mem: Box::new([0; 0x10000]) }
    }

    pub fn read(&self, addr: u16) -> u8 {
        self.mem[addr as usize]
    }
    pub fn write(&mut self, addr: u16, v: u8) {
        self.mem[addr as usize] = v;
    }
    fn read16(&self, addr: u16) -> u16 {
        self.read(addr) as u16 | (self.read(addr.wrapping_add(1)) as u16) << 8
    }

    pub fn load(&mut self, program: &[u8]) {
        self.mem[0x8000..0x8000 + program.len()].copy_from_slice(program);
        self.write(0xFFFC, 0x00);
        self.write(0xFFFD, 0x80);
    }

    pub fn reset(&mut self) {
        self.a = 0;
        self.x = 0;
        self.y = 0;
        self.sp = 0xFD;
        self.status = FLAG_I | FLAG_U;
        self.pc = self.read16(0xFFFC);
    }

    pub fn load_and_run(&mut self, program: &[u8]) {
        self.load(program);
        self.reset();
        self.run();
    }

    fn flag(&self, f: u8) -> bool {
        self.status & f != 0
    }
    fn set_flag(&mut self, f: u8, on: bool) {
        if on {
            self.status |= f
        } else {
            self.status &= !f
        }
    }
    fn set_zn(&mut self, v: u8) -> u8 {
        self.set_flag(FLAG_Z, v == 0);
        self.set_flag(FLAG_N, v & 0x80 != 0);
        v
    }

    fn push(&mut self, v: u8) {
        self.write(0x0100 + self.sp as u16, v);
        self.sp = self.sp.wrapping_sub(1);
    }
    fn pop(&mut self) -> u8 {
        self.sp = self.sp.wrapping_add(1);
        self.read(0x0100 + self.sp as u16)
    }

    fn fetch(&mut self) -> u8 {
        let v = self.read(self.pc);
        self.pc = self.pc.wrapping_add(1);
        v
    }
    fn fetch16(&mut self) -> u16 {
        let lo = self.fetch() as u16;
        lo | (self.fetch() as u16) << 8
    }

    fn addr(&mut self, m: Mode) -> u16 {
        match m {
            Mode::Imm => {
                let a = self.pc;
                self.pc = self.pc.wrapping_add(1);
                a
            }
            Mode::Zp => self.fetch() as u16,
            Mode::ZpX => self.fetch().wrapping_add(self.x) as u16,
            Mode::ZpY => self.fetch().wrapping_add(self.y) as u16,
            Mode::Abs => self.fetch16(),
            Mode::AbsX => self.fetch16().wrapping_add(self.x as u16),
            Mode::AbsY => self.fetch16().wrapping_add(self.y as u16),
            Mode::IndX => {
                // Pointer lives in the zero page and wraps within it.
                let p = self.fetch().wrapping_add(self.x);
                self.read(p as u16) as u16 | (self.read(p.wrapping_add(1) as u16) as u16) << 8
            }
            Mode::IndY => {
                let p = self.fetch();
                let base = self.read(p as u16) as u16 | (self.read(p.wrapping_add(1) as u16) as u16) << 8;
                base.wrapping_add(self.y as u16)
            }
        }
    }

    fn adc(&mut self, v: u8) {
        let c = self.flag(FLAG_C) as u16;
        let sum = self.a as u16 + v as u16 + c;
        let r = sum as u8;
        self.set_flag(FLAG_C, sum > 0xFF);
        // Overflow: operands share a sign that the result does not.
        self.set_flag(FLAG_V, (self.a ^ r) & (v ^ r) & 0x80 != 0);
        self.a = self.set_zn(r);
    }

    fn compare(&mut self, reg: u8, v: u8) {
        self.set_flag(FLAG_C, reg >= v);
        self.set_zn(reg.wrapping_sub(v));
    }

    fn branch(&mut self, cond: bool) {
        let off = self.fetch() as i8;
        if cond {
            self.pc = self.pc.wrapping_add(off as u16);
        }
    }

    fn shift(&mut self, op: u8, v: u8) -> u8 {
        let old_c = self.flag(FLAG_C) as u8;
        let (r, c) = match op {
            0 => (v << 1, v & 0x80 != 0),            // ASL
            1 => (v >> 1, v & 1 != 0),               // LSR
            2 => (v << 1 | old_c, v & 0x80 != 0),    // ROL
            _ => (v >> 1 | old_c << 7, v & 1 != 0),  // ROR
        };
        self.set_flag(FLAG_C, c);
        self.set_zn(r)
    }

    /// Runs until BRK.
    pub fn run(&mut self) {
        while self.step() {}
    }

    /// Executes one instruction; returns false on BRK.
    pub fn step(&mut self) -> bool {
        use Mode::*;
        let op = self.fetch();
        match op {
            0x00 => return false,
            0xEA => {}
            // LDA / LDX / LDY
            0xA9 | 0xA5 | 0xB5 | 0xAD | 0xBD | 0xB9 | 0xA1 | 0xB1 => {
                let m = match op { 0xA9 => Imm, 0xA5 => Zp, 0xB5 => ZpX, 0xAD => Abs, 0xBD => AbsX, 0xB9 => AbsY, 0xA1 => IndX, _ => IndY };
                let a = self.addr(m);
                self.a = self.set_zn(self.read(a));
            }
            0xA2 | 0xA6 | 0xB6 | 0xAE | 0xBE => {
                let m = match op { 0xA2 => Imm, 0xA6 => Zp, 0xB6 => ZpY, 0xAE => Abs, _ => AbsY };
                let a = self.addr(m);
                self.x = self.set_zn(self.read(a));
            }
            0xA0 | 0xA4 | 0xB4 | 0xAC | 0xBC => {
                let m = match op { 0xA0 => Imm, 0xA4 => Zp, 0xB4 => ZpX, 0xAC => Abs, _ => AbsX };
                let a = self.addr(m);
                self.y = self.set_zn(self.read(a));
            }
            // STA / STX / STY
            0x85 | 0x95 | 0x8D | 0x9D | 0x99 | 0x81 | 0x91 => {
                let m = match op { 0x85 => Zp, 0x95 => ZpX, 0x8D => Abs, 0x9D => AbsX, 0x99 => AbsY, 0x81 => IndX, _ => IndY };
                let a = self.addr(m);
                self.write(a, self.a);
            }
            0x86 | 0x96 | 0x8E => {
                let m = match op { 0x86 => Zp, 0x96 => ZpY, _ => Abs };
                let a = self.addr(m);
                self.write(a, self.x);
            }
            0x84 | 0x94 | 0x8C => {
                let m = match op { 0x84 => Zp, 0x94 => ZpX, _ => Abs };
                let a = self.addr(m);
                self.write(a, self.y);
            }
            // Transfers
            0xAA => self.x = self.set_zn(self.a),
            0xA8 => self.y = self.set_zn(self.a),
            0x8A => self.a = self.set_zn(self.x),
            0x98 => self.a = self.set_zn(self.y),
            0xBA => self.x = self.set_zn(self.sp),
            0x9A => self.sp = self.x,
            // Inc / dec
            0xE8 => self.x = self.set_zn(self.x.wrapping_add(1)),
            0xC8 => self.y = self.set_zn(self.y.wrapping_add(1)),
            0xCA => self.x = self.set_zn(self.x.wrapping_sub(1)),
            0x88 => self.y = self.set_zn(self.y.wrapping_sub(1)),
            0xE6 | 0xF6 | 0xEE | 0xFE | 0xC6 | 0xD6 | 0xCE | 0xDE => {
                let m = match op & 0x0F { 0x6 if op & 0x10 == 0 => Zp, 0x6 => ZpX, 0xE if op & 0x10 == 0 => Abs, _ => AbsX };
                let a = self.addr(m);
                let d = if op & 0x20 != 0 { 1u8 } else { 0xFF };
                let v = self.read(a).wrapping_add(d);
                self.write(a, v);
                self.set_zn(v);
            }
            // ADC / SBC (SBC is ADC of the complement)
            0x69 | 0x65 | 0x75 | 0x6D | 0x7D | 0x79 | 0x61 | 0x71
            | 0xE9 | 0xE5 | 0xF5 | 0xED | 0xFD | 0xF9 | 0xE1 | 0xF1 => {
                let m = match op & 0x1F { 0x09 => Imm, 0x05 => Zp, 0x15 => ZpX, 0x0D => Abs, 0x1D => AbsX, 0x19 => AbsY, 0x01 => IndX, _ => IndY };
                let a = self.addr(m);
                let v = self.read(a);
                self.adc(if op >= 0xE0 { !v } else { v });
            }
            // AND / ORA / EOR / CMP
            0x29 | 0x25 | 0x35 | 0x2D | 0x3D | 0x39 | 0x21 | 0x31
            | 0x09 | 0x05 | 0x15 | 0x0D | 0x1D | 0x19 | 0x01 | 0x11
            | 0x49 | 0x45 | 0x55 | 0x4D | 0x5D | 0x59 | 0x41 | 0x51
            | 0xC9 | 0xC5 | 0xD5 | 0xCD | 0xDD | 0xD9 | 0xC1 | 0xD1 => {
                let m = match op & 0x1F { 0x09 => Imm, 0x05 => Zp, 0x15 => ZpX, 0x0D => Abs, 0x1D => AbsX, 0x19 => AbsY, 0x01 => IndX, _ => IndY };
                let a = self.addr(m);
                let v = self.read(a);
                match op >> 5 {
                    0 => self.a = self.set_zn(self.a | v),
                    1 => self.a = self.set_zn(self.a & v),
                    2 => self.a = self.set_zn(self.a ^ v),
                    _ => self.compare(self.a, v),
                }
            }
            0xE0 | 0xE4 | 0xEC => {
                let a = self.addr(match op { 0xE0 => Imm, 0xE4 => Zp, _ => Abs });
                self.compare(self.x, self.read(a));
            }
            0xC0 | 0xC4 | 0xCC => {
                let a = self.addr(match op { 0xC0 => Imm, 0xC4 => Zp, _ => Abs });
                self.compare(self.y, self.read(a));
            }
            0x24 | 0x2C => {
                let a = self.addr(if op == 0x24 { Zp } else { Abs });
                let v = self.read(a);
                self.set_flag(FLAG_Z, self.a & v == 0);
                self.set_flag(FLAG_V, v & 0x40 != 0);
                self.set_flag(FLAG_N, v & 0x80 != 0);
            }
            // Shifts: accumulator
            0x0A | 0x4A | 0x2A | 0x6A => {
                let k = match op { 0x0A => 0, 0x4A => 1, 0x2A => 2, _ => 3 };
                self.a = self.shift(k, self.a);
            }
            // Shifts: memory
            0x06 | 0x16 | 0x0E | 0x1E | 0x46 | 0x56 | 0x4E | 0x5E
            | 0x26 | 0x36 | 0x2E | 0x3E | 0x66 | 0x76 | 0x6E | 0x7E => {
                let m = match op & 0x1F { 0x06 => Zp, 0x16 => ZpX, 0x0E => Abs, _ => AbsX };
                let k = match op >> 5 { 0 => 0, 2 => 1, 1 => 2, _ => 3 };
                let a = self.addr(m);
                let v = self.shift(k, self.read(a));
                self.write(a, v);
            }
            // Flags
            0x18 => self.set_flag(FLAG_C, false),
            0x38 => self.set_flag(FLAG_C, true),
            0x58 => self.set_flag(FLAG_I, false),
            0x78 => self.set_flag(FLAG_I, true),
            0xB8 => self.set_flag(FLAG_V, false),
            // Branches
            0x10 => self.branch(!self.flag(FLAG_N)),
            0x30 => self.branch(self.flag(FLAG_N)),
            0x50 => self.branch(!self.flag(FLAG_V)),
            0x70 => self.branch(self.flag(FLAG_V)),
            0x90 => self.branch(!self.flag(FLAG_C)),
            0xB0 => self.branch(self.flag(FLAG_C)),
            0xD0 => self.branch(!self.flag(FLAG_Z)),
            0xF0 => self.branch(self.flag(FLAG_Z)),
            // Jumps / subroutines
            0x4C => self.pc = self.fetch16(),
            0x6C => {
                let p = self.fetch16();
                // Hardware bug: the high byte is fetched without carrying across a page.
                let hi = (p & 0xFF00) | (p.wrapping_add(1) & 0x00FF);
                self.pc = self.read(p) as u16 | (self.read(hi) as u16) << 8;
            }
            0x20 => {
                let t = self.fetch16();
                let ret = self.pc.wrapping_sub(1);
                self.push((ret >> 8) as u8);
                self.push(ret as u8);
                self.pc = t;
            }
            0x60 => {
                let lo = self.pop() as u16;
                let hi = self.pop() as u16;
                self.pc = (hi << 8 | lo).wrapping_add(1);
            }
            0x40 => {
                self.status = (self.pop() & !FLAG_B) | FLAG_U;
                let lo = self.pop() as u16;
                self.pc = (self.pop() as u16) << 8 | lo;
            }
            // Stack
            0x48 => self.push(self.a),
            0x08 => self.push(self.status | FLAG_B | FLAG_U),
            0x68 => {
                let v = self.pop();
                self.a = self.set_zn(v);
            }
            0x28 => self.status = (self.pop() & !FLAG_B) | FLAG_U,
            _ => panic!("unimplemented opcode {:#04x} at {:#06x}", op, self.pc.wrapping_sub(1)),
        }
        true
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn run(p: &[u8]) -> Cpu {
        let mut c = Cpu::new();
        c.load_and_run(p);
        c
    }

    #[test]
    fn lda_flags() {
        assert!(run(&[0xA9, 0x00, 0x00]).status & FLAG_Z != 0);
        assert!(run(&[0xA9, 0x80, 0x00]).status & FLAG_N != 0);
    }

    #[test]
    fn tax_inx_overflow_wraps() {
        let c = run(&[0xA9, 0xFF, 0xAA, 0xE8, 0xE8, 0x00]);
        assert_eq!(c.x, 1);
    }

    #[test]
    fn adc_carry_and_overflow() {
        // 0x50 + 0x50 = 0xA0: signed overflow, no carry
        let c = run(&[0xA9, 0x50, 0x69, 0x50, 0x00]);
        assert_eq!(c.a, 0xA0);
        assert!(c.status & FLAG_V != 0 && c.status & FLAG_C == 0);
        // 0xFF + 0x01 = 0x00 with carry
        let c = run(&[0xA9, 0xFF, 0x69, 0x01, 0x00]);
        assert!(c.a == 0 && c.status & FLAG_C != 0 && c.status & FLAG_V == 0);
    }

    #[test]
    fn sbc_borrow() {
        // SEC; 5 - 3 = 2, carry stays set (no borrow)
        let c = run(&[0x38, 0xA9, 0x05, 0xE9, 0x03, 0x00]);
        assert_eq!(c.a, 2);
        assert!(c.status & FLAG_C != 0);
        // SEC; 3 - 5 = 0xFE, carry clear (borrow)
        let c = run(&[0x38, 0xA9, 0x03, 0xE9, 0x05, 0x00]);
        assert_eq!(c.a, 0xFE);
        assert!(c.status & FLAG_C == 0);
    }

    #[test]
    fn zero_page_x_wraps() {
        let mut c = Cpu::new();
        c.write(0x0004, 0x77);
        c.load_and_run(&[0xA2, 0x05, 0xB5, 0xFF, 0x00]); // LDX #5; LDA $FF,X -> $04
        assert_eq!(c.a, 0x77);
    }

    #[test]
    fn indirect_x_and_y() {
        let mut c = Cpu::new();
        c.write(0x0024, 0x00);
        c.write(0x0025, 0x03);
        c.write(0x0300, 0xAB);
        c.write(0x0304, 0xCD);
        c.load_and_run(&[0xA2, 0x04, 0xA1, 0x20, 0x00]); // LDX #4; LDA ($20,X)
        assert_eq!(c.a, 0xAB);
        c.load_and_run(&[0xA0, 0x04, 0xB1, 0x24, 0x00]); // LDY #4; LDA ($24),Y
        assert_eq!(c.a, 0xCD);
    }

    #[test]
    fn jsr_rts() {
        // JSR $8006; LDX #1; BRK; sub: LDA #9; RTS
        let c = run(&[0x20, 0x07, 0x80, 0xA2, 0x01, 0x00, 0x00, 0xA9, 0x09, 0x60]);
        assert_eq!((c.a, c.x), (9, 1));
        assert_eq!(c.sp, 0xFD);
    }

    #[test]
    fn jmp_indirect_page_bug() {
        let mut c = Cpu::new();
        c.write(0x02FF, 0x00);
        c.write(0x0200, 0x90); // bug: high byte read from $0200, not $0300
        c.write(0x0300, 0x11);
        c.load(&[0x6C, 0xFF, 0x02]);
        c.reset();
        c.step();
        assert_eq!(c.pc, 0x9000);
    }

    #[test]
    fn shifts_and_rotates() {
        let c = run(&[0xA9, 0x81, 0x0A, 0x00]); // ASL: 0x02, C set
        assert!(c.a == 0x02 && c.status & FLAG_C != 0);
        let c = run(&[0x38, 0xA9, 0x01, 0x6A, 0x00]); // SEC; ROR: 0x80, C set
        assert!(c.a == 0x80 && c.status & FLAG_C != 0);
        let c = run(&[0x38, 0xA9, 0x80, 0x2A, 0x00]); // SEC; ROL: 0x01, C set
        assert!(c.a == 0x01 && c.status & FLAG_C != 0);
    }

    #[test]
    fn inc_dec_memory() {
        let c = run(&[0xA9, 0x00, 0x85, 0x10, 0xC6, 0x10, 0x00]); // DEC $10 -> 0xFF
        assert_eq!(c.read(0x10), 0xFF);
        assert!(c.status & FLAG_N != 0);
        let c = run(&[0xE6, 0x10, 0xE6, 0x10, 0x00]);
        assert_eq!(c.read(0x10), 2);
    }

    #[test]
    fn stack_push_pull() {
        let c = run(&[0xA9, 0x42, 0x48, 0xA9, 0x00, 0x68, 0x00]);
        assert_eq!(c.a, 0x42);
    }

    #[test]
    fn cmp_sets_carry_zero() {
        let c = run(&[0xA9, 0x10, 0xC9, 0x10, 0x00]);
        assert!(c.status & FLAG_Z != 0 && c.status & FLAG_C != 0);
        let c = run(&[0xA9, 0x10, 0xC9, 0x20, 0x00]);
        assert!(c.status & FLAG_C == 0 && c.status & FLAG_N != 0);
    }

    #[test]
    fn bit_test() {
        let mut c = Cpu::new();
        c.write(0x10, 0xC0);
        c.load_and_run(&[0xA9, 0x01, 0x24, 0x10, 0x00]);
        assert!(c.status & FLAG_Z != 0 && c.status & FLAG_V != 0 && c.status & FLAG_N != 0);
    }

    #[test]
    fn loop_sum() {
        // sum of 1..=10 = 55
        let c = run(&[0xA2, 0x0A, 0xA9, 0x00, 0x18, 0x86, 0x10, 0x65, 0x10, 0xCA, 0xD0, 0xF9, 0x8D, 0x00, 0x02, 0x00]);
        assert_eq!(c.a, 55);
        assert_eq!(c.read(0x0200), 55);
    }
}
