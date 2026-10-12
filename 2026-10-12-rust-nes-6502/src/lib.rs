//! A MOS 6502 CPU core (official opcodes), as built in the CPU chapter of
//! "Writing NES Emulator in Rust". No cycle counting, no decimal mode (the NES's
//! 2A03 has none), no illegal opcodes.

pub const FLAG_C: u8 = 0b0000_0001;
pub const FLAG_Z: u8 = 0b0000_0010;
pub const FLAG_I: u8 = 0b0000_0100;
pub const FLAG_D: u8 = 0b0000_1000;
pub const FLAG_B: u8 = 0b0001_0000;
pub const FLAG_U: u8 = 0b0010_0000;
pub const FLAG_V: u8 = 0b0100_0000;
pub const FLAG_N: u8 = 0b1000_0000;

const STACK_BASE: u16 = 0x0100;
const RESET_VECTOR: u16 = 0xFFFC;
const IRQ_VECTOR: u16 = 0xFFFE;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Mode {
    Imp,
    Acc,
    Imm,
    Zp,
    ZpX,
    ZpY,
    Abs,
    AbsX,
    AbsY,
    Ind,
    IndX,
    IndY,
    Rel,
}

impl Mode {
    /// Total instruction length in bytes, opcode included.
    pub fn size(self) -> u16 {
        match self {
            Mode::Imp | Mode::Acc => 1,
            Mode::Imm | Mode::Zp | Mode::ZpX | Mode::ZpY | Mode::IndX | Mode::IndY | Mode::Rel => 2,
            Mode::Abs | Mode::AbsX | Mode::AbsY | Mode::Ind => 3,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Op {
    Adc, And, Asl, Bcc, Bcs, Beq, Bit, Bmi, Bne, Bpl, Brk, Bvc, Bvs, Clc, Cld, Cli, Clv,
    Cmp, Cpx, Cpy, Dec, Dex, Dey, Eor, Inc, Inx, Iny, Jmp, Jsr, Lda, Ldx, Ldy, Lsr, Nop,
    Ora, Pha, Php, Pla, Plp, Rol, Ror, Rti, Rts, Sbc, Sec, Sed, Sei, Sta, Stx, Sty, Tax,
    Tay, Tsx, Txa, Txs, Tya,
}

use Mode::*;
use Op::*;

/// Every official opcode: (byte, operation, addressing mode).
pub const OPCODES: &[(u8, Op, Mode)] = &[
    (0x69, Adc, Imm), (0x65, Adc, Zp), (0x75, Adc, ZpX), (0x6D, Adc, Abs), (0x7D, Adc, AbsX), (0x79, Adc, AbsY), (0x61, Adc, IndX), (0x71, Adc, IndY),
    (0x29, And, Imm), (0x25, And, Zp), (0x35, And, ZpX), (0x2D, And, Abs), (0x3D, And, AbsX), (0x39, And, AbsY), (0x21, And, IndX), (0x31, And, IndY),
    (0x0A, Asl, Acc), (0x06, Asl, Zp), (0x16, Asl, ZpX), (0x0E, Asl, Abs), (0x1E, Asl, AbsX),
    (0x90, Bcc, Rel), (0xB0, Bcs, Rel), (0xF0, Beq, Rel), (0x30, Bmi, Rel), (0xD0, Bne, Rel), (0x10, Bpl, Rel), (0x50, Bvc, Rel), (0x70, Bvs, Rel),
    (0x24, Bit, Zp), (0x2C, Bit, Abs), (0x00, Brk, Imp),
    (0x18, Clc, Imp), (0xD8, Cld, Imp), (0x58, Cli, Imp), (0xB8, Clv, Imp),
    (0xC9, Cmp, Imm), (0xC5, Cmp, Zp), (0xD5, Cmp, ZpX), (0xCD, Cmp, Abs), (0xDD, Cmp, AbsX), (0xD9, Cmp, AbsY), (0xC1, Cmp, IndX), (0xD1, Cmp, IndY),
    (0xE0, Cpx, Imm), (0xE4, Cpx, Zp), (0xEC, Cpx, Abs),
    (0xC0, Cpy, Imm), (0xC4, Cpy, Zp), (0xCC, Cpy, Abs),
    (0xC6, Dec, Zp), (0xD6, Dec, ZpX), (0xCE, Dec, Abs), (0xDE, Dec, AbsX), (0xCA, Dex, Imp), (0x88, Dey, Imp),
    (0x49, Eor, Imm), (0x45, Eor, Zp), (0x55, Eor, ZpX), (0x4D, Eor, Abs), (0x5D, Eor, AbsX), (0x59, Eor, AbsY), (0x41, Eor, IndX), (0x51, Eor, IndY),
    (0xE6, Inc, Zp), (0xF6, Inc, ZpX), (0xEE, Inc, Abs), (0xFE, Inc, AbsX), (0xE8, Inx, Imp), (0xC8, Iny, Imp),
    (0x4C, Jmp, Abs), (0x6C, Jmp, Ind), (0x20, Jsr, Abs),
    (0xA9, Lda, Imm), (0xA5, Lda, Zp), (0xB5, Lda, ZpX), (0xAD, Lda, Abs), (0xBD, Lda, AbsX), (0xB9, Lda, AbsY), (0xA1, Lda, IndX), (0xB1, Lda, IndY),
    (0xA2, Ldx, Imm), (0xA6, Ldx, Zp), (0xB6, Ldx, ZpY), (0xAE, Ldx, Abs), (0xBE, Ldx, AbsY),
    (0xA0, Ldy, Imm), (0xA4, Ldy, Zp), (0xB4, Ldy, ZpX), (0xAC, Ldy, Abs), (0xBC, Ldy, AbsX),
    (0x4A, Lsr, Acc), (0x46, Lsr, Zp), (0x56, Lsr, ZpX), (0x4E, Lsr, Abs), (0x5E, Lsr, AbsX),
    (0xEA, Nop, Imp),
    (0x09, Ora, Imm), (0x05, Ora, Zp), (0x15, Ora, ZpX), (0x0D, Ora, Abs), (0x1D, Ora, AbsX), (0x19, Ora, AbsY), (0x01, Ora, IndX), (0x11, Ora, IndY),
    (0x48, Pha, Imp), (0x08, Php, Imp), (0x68, Pla, Imp), (0x28, Plp, Imp),
    (0x2A, Rol, Acc), (0x26, Rol, Zp), (0x36, Rol, ZpX), (0x2E, Rol, Abs), (0x3E, Rol, AbsX),
    (0x6A, Ror, Acc), (0x66, Ror, Zp), (0x76, Ror, ZpX), (0x6E, Ror, Abs), (0x7E, Ror, AbsX),
    (0x40, Rti, Imp), (0x60, Rts, Imp),
    (0xE9, Sbc, Imm), (0xE5, Sbc, Zp), (0xF5, Sbc, ZpX), (0xED, Sbc, Abs), (0xFD, Sbc, AbsX), (0xF9, Sbc, AbsY), (0xE1, Sbc, IndX), (0xF1, Sbc, IndY),
    (0x38, Sec, Imp), (0xF8, Sed, Imp), (0x78, Sei, Imp),
    (0x85, Sta, Zp), (0x95, Sta, ZpX), (0x8D, Sta, Abs), (0x9D, Sta, AbsX), (0x99, Sta, AbsY), (0x81, Sta, IndX), (0x91, Sta, IndY),
    (0x86, Stx, Zp), (0x96, Stx, ZpY), (0x8E, Stx, Abs),
    (0x84, Sty, Zp), (0x94, Sty, ZpX), (0x8C, Sty, Abs),
    (0xAA, Tax, Imp), (0xA8, Tay, Imp), (0xBA, Tsx, Imp), (0x8A, Txa, Imp), (0x9A, Txs, Imp), (0x98, Tya, Imp),
];

fn decode(opcode: u8) -> Option<(Op, Mode)> {
    OPCODES.iter().find(|e| e.0 == opcode).map(|e| (e.1, e.2))
}

#[derive(Debug, PartialEq, Eq)]
pub enum StepError {
    /// The byte at `pc` is not an official opcode.
    UnknownOpcode { opcode: u8, pc: u16 },
}

pub struct Cpu {
    pub a: u8,
    pub x: u8,
    pub y: u8,
    pub sp: u8,
    pub pc: u16,
    pub status: u8,
    pub memory: Box<[u8; 0x10000]>,
}

impl Default for Cpu {
    fn default() -> Self {
        Self::new()
    }
}

impl Cpu {
    pub fn new() -> Self {
        Cpu { a: 0, x: 0, y: 0, sp: 0xFD, pc: 0, status: FLAG_I | FLAG_U, memory: Box::new([0; 0x10000]) }
    }

    pub fn read(&self, addr: u16) -> u8 {
        self.memory[addr as usize]
    }

    pub fn write(&mut self, addr: u16, v: u8) {
        self.memory[addr as usize] = v;
    }

    pub fn read_u16(&self, addr: u16) -> u16 {
        u16::from_le_bytes([self.read(addr), self.read(addr.wrapping_add(1))])
    }

    /// Copy `program` into memory at `origin` and point the reset vector at it.
    pub fn load(&mut self, origin: u16, program: &[u8]) {
        let start = origin as usize;
        self.memory[start..start + program.len()].copy_from_slice(program);
        self.write(RESET_VECTOR, origin as u8);
        self.write(RESET_VECTOR + 1, (origin >> 8) as u8);
    }

    pub fn reset(&mut self) {
        self.a = 0;
        self.x = 0;
        self.y = 0;
        self.sp = 0xFD;
        self.status = FLAG_I | FLAG_U;
        self.pc = self.read_u16(RESET_VECTOR);
    }

    pub fn flag(&self, f: u8) -> bool {
        self.status & f != 0
    }

    fn set_flag(&mut self, f: u8, on: bool) {
        if on {
            self.status |= f;
        } else {
            self.status &= !f;
        }
    }

    fn set_zn(&mut self, v: u8) -> u8 {
        self.set_flag(FLAG_Z, v == 0);
        self.set_flag(FLAG_N, v & 0x80 != 0);
        v
    }

    fn push(&mut self, v: u8) {
        self.write(STACK_BASE + self.sp as u16, v);
        self.sp = self.sp.wrapping_sub(1);
    }

    fn pull(&mut self) -> u8 {
        self.sp = self.sp.wrapping_add(1);
        self.read(STACK_BASE + self.sp as u16)
    }

    fn push_u16(&mut self, v: u16) {
        self.push((v >> 8) as u8);
        self.push(v as u8);
    }

    fn pull_u16(&mut self) -> u16 {
        let lo = self.pull() as u16;
        lo | (self.pull() as u16) << 8
    }

    /// Effective address of the operand. `pc` points at the first operand byte.
    fn operand_addr(&self, mode: Mode) -> u16 {
        let pc = self.pc;
        match mode {
            Imm => pc,
            Zp => self.read(pc) as u16,
            ZpX => self.read(pc).wrapping_add(self.x) as u16,
            ZpY => self.read(pc).wrapping_add(self.y) as u16,
            Abs => self.read_u16(pc),
            AbsX => self.read_u16(pc).wrapping_add(self.x as u16),
            AbsY => self.read_u16(pc).wrapping_add(self.y as u16),
            IndX => {
                let ptr = self.read(pc).wrapping_add(self.x);
                u16::from_le_bytes([self.read(ptr as u16), self.read(ptr.wrapping_add(1) as u16)])
            }
            IndY => {
                let ptr = self.read(pc);
                let base = u16::from_le_bytes([self.read(ptr as u16), self.read(ptr.wrapping_add(1) as u16)]);
                base.wrapping_add(self.y as u16)
            }
            Ind => {
                // Hardware quirk: the high byte is fetched without carrying into the next page.
                let ptr = self.read_u16(pc);
                let hi_addr = (ptr & 0xFF00) | (ptr.wrapping_add(1) & 0x00FF);
                u16::from_le_bytes([self.read(ptr), self.read(hi_addr)])
            }
            Rel | Imp | Acc => unreachable!("mode {:?} has no memory operand", mode),
        }
    }

    fn add_with_carry(&mut self, m: u8) {
        let sum = self.a as u16 + m as u16 + self.flag(FLAG_C) as u16;
        let result = sum as u8;
        self.set_flag(FLAG_C, sum > 0xFF);
        self.set_flag(FLAG_V, (self.a ^ result) & (m ^ result) & 0x80 != 0);
        self.a = self.set_zn(result);
    }

    fn compare(&mut self, reg: u8, m: u8) {
        self.set_flag(FLAG_C, reg >= m);
        self.set_zn(reg.wrapping_sub(m));
    }

    fn branch(&mut self, cond: bool) {
        let offset = self.read(self.pc) as i8;
        let next = self.pc.wrapping_add(1);
        self.pc = if cond { next.wrapping_add(offset as u16) } else { next };
    }

    /// Run a read-modify-write shift/rotate on the accumulator or a memory operand.
    fn shift(&mut self, mode: Mode, f: impl Fn(&mut Cpu, u8) -> u8) {
        if mode == Acc {
            let v = self.a;
            self.a = f(self, v);
        } else {
            let addr = self.operand_addr(mode);
            let v = self.read(addr);
            let r = f(self, v);
            self.write(addr, r);
        }
    }

    /// Execute one instruction. Returns the opcode that ran.
    pub fn step(&mut self) -> Result<u8, StepError> {
        let at = self.pc;
        let opcode = self.read(at);
        let (op, mode) = decode(opcode).ok_or(StepError::UnknownOpcode { opcode, pc: at })?;
        self.pc = at.wrapping_add(1);

        match op {
            Lda | Ldx | Ldy => {
                let v = self.read(self.operand_addr(mode));
                let v = self.set_zn(v);
                match op { Lda => self.a = v, Ldx => self.x = v, _ => self.y = v }
            }
            Sta => self.write(self.operand_addr(mode), self.a),
            Stx => self.write(self.operand_addr(mode), self.x),
            Sty => self.write(self.operand_addr(mode), self.y),
            Tax => self.x = self.set_zn(self.a),
            Tay => self.y = self.set_zn(self.a),
            Txa => self.a = self.set_zn(self.x),
            Tya => self.a = self.set_zn(self.y),
            Tsx => self.x = self.set_zn(self.sp),
            Txs => self.sp = self.x,
            Pha => self.push(self.a),
            Php => self.push(self.status | FLAG_B | FLAG_U),
            Pla => { let v = self.pull(); self.a = self.set_zn(v); }
            Plp => { self.status = (self.pull() & !FLAG_B) | FLAG_U; }
            And => { let v = self.a & self.read(self.operand_addr(mode)); self.a = self.set_zn(v); }
            Eor => { let v = self.a ^ self.read(self.operand_addr(mode)); self.a = self.set_zn(v); }
            Ora => { let v = self.a | self.read(self.operand_addr(mode)); self.a = self.set_zn(v); }
            Bit => {
                let m = self.read(self.operand_addr(mode));
                self.set_flag(FLAG_Z, self.a & m == 0);
                self.set_flag(FLAG_V, m & 0x40 != 0);
                self.set_flag(FLAG_N, m & 0x80 != 0);
            }
            Adc => { let m = self.read(self.operand_addr(mode)); self.add_with_carry(m); }
            Sbc => { let m = self.read(self.operand_addr(mode)); self.add_with_carry(!m); }
            Cmp => { let m = self.read(self.operand_addr(mode)); self.compare(self.a, m); }
            Cpx => { let m = self.read(self.operand_addr(mode)); self.compare(self.x, m); }
            Cpy => { let m = self.read(self.operand_addr(mode)); self.compare(self.y, m); }
            Inc | Dec => {
                let addr = self.operand_addr(mode);
                let v = self.read(addr);
                let r = if op == Inc { v.wrapping_add(1) } else { v.wrapping_sub(1) };
                let r = self.set_zn(r);
                self.write(addr, r);
            }
            Inx => self.x = self.set_zn(self.x.wrapping_add(1)),
            Iny => self.y = self.set_zn(self.y.wrapping_add(1)),
            Dex => self.x = self.set_zn(self.x.wrapping_sub(1)),
            Dey => self.y = self.set_zn(self.y.wrapping_sub(1)),
            Asl => self.shift(mode, |c, v| { c.set_flag(FLAG_C, v & 0x80 != 0); c.set_zn(v << 1) }),
            Lsr => self.shift(mode, |c, v| { c.set_flag(FLAG_C, v & 1 != 0); c.set_zn(v >> 1) }),
            Rol => self.shift(mode, |c, v| {
                let r = (v << 1) | c.flag(FLAG_C) as u8;
                c.set_flag(FLAG_C, v & 0x80 != 0);
                c.set_zn(r)
            }),
            Ror => self.shift(mode, |c, v| {
                let r = (v >> 1) | (c.flag(FLAG_C) as u8) << 7;
                c.set_flag(FLAG_C, v & 1 != 0);
                c.set_zn(r)
            }),
            Jmp => { self.pc = self.operand_addr(mode); return Ok(opcode); }
            Jsr => {
                let target = self.operand_addr(mode);
                // The 6502 pushes the address of the last byte of the JSR, not the next instruction.
                self.push_u16(self.pc.wrapping_add(1));
                self.pc = target;
                return Ok(opcode);
            }
            Rts => { self.pc = self.pull_u16().wrapping_add(1); return Ok(opcode); }
            Rti => {
                self.status = (self.pull() & !FLAG_B) | FLAG_U;
                self.pc = self.pull_u16();
                return Ok(opcode);
            }
            Brk => {
                // BRK has a padding byte, so the return address skips it.
                self.push_u16(self.pc.wrapping_add(1));
                self.push(self.status | FLAG_B | FLAG_U);
                self.set_flag(FLAG_I, true);
                self.pc = self.read_u16(IRQ_VECTOR);
                return Ok(opcode);
            }
            Bcc => self.branch(!self.flag(FLAG_C)),
            Bcs => self.branch(self.flag(FLAG_C)),
            Beq => self.branch(self.flag(FLAG_Z)),
            Bne => self.branch(!self.flag(FLAG_Z)),
            Bmi => self.branch(self.flag(FLAG_N)),
            Bpl => self.branch(!self.flag(FLAG_N)),
            Bvs => self.branch(self.flag(FLAG_V)),
            Bvc => self.branch(!self.flag(FLAG_V)),
            Clc => self.set_flag(FLAG_C, false),
            Sec => self.set_flag(FLAG_C, true),
            Cld => self.set_flag(FLAG_D, false),
            Sed => self.set_flag(FLAG_D, true),
            Cli => self.set_flag(FLAG_I, false),
            Sei => self.set_flag(FLAG_I, true),
            Clv => self.set_flag(FLAG_V, false),
            Nop => {}
        }

        // Branches already consumed their operand; everything else skips it here.
        if !matches!(op, Bcc | Bcs | Beq | Bne | Bmi | Bpl | Bvs | Bvc) {
            self.pc = self.pc.wrapping_add(mode.size() - 1);
        }
        Ok(opcode)
    }

    /// Run until a BRK executes (or an unknown opcode / `max_steps` is hit).
    pub fn run(&mut self, max_steps: usize) -> Result<usize, StepError> {
        for n in 1..=max_steps {
            if self.step()? == 0x00 {
                return Ok(n);
            }
        }
        Ok(max_steps)
    }

    /// Load at `origin`, reset, and run to the first BRK.
    pub fn load_and_run(&mut self, origin: u16, program: &[u8]) -> Result<usize, StepError> {
        self.load(origin, program);
        self.reset();
        self.run(1_000_000)
    }
}
