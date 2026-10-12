use nes6502::*;

fn run(program: &[u8]) -> Cpu {
    let mut cpu = Cpu::new();
    cpu.load_and_run(0x8000, program).unwrap();
    cpu
}

#[test]
fn lda_sets_zero_and_negative() {
    let c = run(&[0xA9, 0x00, 0x00]);
    assert!(c.flag(FLAG_Z) && !c.flag(FLAG_N));
    let c = run(&[0xA9, 0x80, 0x00]);
    assert!(!c.flag(FLAG_Z) && c.flag(FLAG_N));
}

#[test]
fn tax_inx_overflow_wraps() {
    let c = run(&[0xA9, 0xFF, 0xAA, 0xE8, 0x00]);
    assert_eq!(c.x, 0);
    assert!(c.flag(FLAG_Z));
}

#[test]
fn adc_carry_and_overflow() {
    // 0x50 + 0x50 = 0xA0: signed overflow, no carry
    let c = run(&[0x18, 0xA9, 0x50, 0x69, 0x50, 0x00]);
    assert_eq!(c.a, 0xA0);
    assert!(c.flag(FLAG_V) && !c.flag(FLAG_C) && c.flag(FLAG_N));
    // 0xFF + 0x01 = 0x00 with carry
    let c = run(&[0x18, 0xA9, 0xFF, 0x69, 0x01, 0x00]);
    assert_eq!(c.a, 0);
    assert!(c.flag(FLAG_C) && c.flag(FLAG_Z) && !c.flag(FLAG_V));
}

#[test]
fn sbc_borrow_semantics() {
    // SEC; 0x50 - 0x30 = 0x20, carry stays set (no borrow)
    let c = run(&[0x38, 0xA9, 0x50, 0xE9, 0x30, 0x00]);
    assert_eq!(c.a, 0x20);
    assert!(c.flag(FLAG_C));
    // SEC; 0x30 - 0x50 = 0xE0, carry clear (borrow)
    let c = run(&[0x38, 0xA9, 0x30, 0xE9, 0x50, 0x00]);
    assert_eq!(c.a, 0xE0);
    assert!(!c.flag(FLAG_C));
    // signed overflow: 0x80 - 0x01 = 0x7F
    let c = run(&[0x38, 0xA9, 0x80, 0xE9, 0x01, 0x00]);
    assert_eq!(c.a, 0x7F);
    assert!(c.flag(FLAG_V));
}

#[test]
fn zero_page_x_wraps_within_page_zero() {
    // LDX #$FF ; LDA #$42 ; STA $10 ; LDA $11,X (=> $10) ; BRK
    let c = run(&[0xA2, 0xFF, 0xA9, 0x42, 0x85, 0x10, 0xA9, 0x00, 0xB5, 0x11, 0x00]);
    assert_eq!(c.a, 0x42);
}

#[test]
fn indirect_x_and_y() {
    // pointer at $20/$21 -> $0300 ; value at $0305
    let mut cpu = Cpu::new();
    cpu.write(0x20, 0x00);
    cpu.write(0x21, 0x03);
    cpu.write(0x0305, 0x99);
    // LDX #$00 ; LDY #$05 ; LDA ($20),Y ; STA $50 ; LDA ($20,X) ; BRK
    cpu.load_and_run(0x8000, &[0xA2, 0x00, 0xA0, 0x05, 0xB1, 0x20, 0x85, 0x50, 0xA1, 0x20, 0x00]).unwrap();
    assert_eq!(cpu.read(0x50), 0x99);
    assert_eq!(cpu.a, cpu.read(0x0300));
}

#[test]
fn shifts_and_rotates() {
    // ASL: 0x81 -> 0x02, carry set
    let c = run(&[0xA9, 0x81, 0x0A, 0x00]);
    assert_eq!(c.a, 0x02);
    assert!(c.flag(FLAG_C));
    // ROR with carry set: 0x02 -> 0x81
    let c = run(&[0x38, 0xA9, 0x02, 0x6A, 0x00]);
    assert_eq!(c.a, 0x81);
    assert!(!c.flag(FLAG_C) && c.flag(FLAG_N));
    // LSR on memory
    let mut cpu = Cpu::new();
    cpu.write(0x10, 0x03);
    cpu.load_and_run(0x8000, &[0x46, 0x10, 0x00]).unwrap();
    assert_eq!(cpu.read(0x10), 0x01);
    assert!(cpu.flag(FLAG_C));
}

#[test]
fn inc_dec_memory() {
    let mut cpu = Cpu::new();
    cpu.write(0x10, 0xFF);
    cpu.load_and_run(0x8000, &[0xE6, 0x10, 0xC6, 0x11, 0x00]).unwrap();
    assert_eq!(cpu.read(0x10), 0x00);
    assert_eq!(cpu.read(0x11), 0xFF);
    assert!(cpu.flag(FLAG_N));
}

#[test]
fn cmp_sets_carry_zero_negative() {
    let c = run(&[0xA9, 0x10, 0xC9, 0x10, 0x00]);
    assert!(c.flag(FLAG_C) && c.flag(FLAG_Z));
    let c = run(&[0xA9, 0x10, 0xC9, 0x20, 0x00]);
    assert!(!c.flag(FLAG_C) && c.flag(FLAG_N));
}

#[test]
fn bit_copies_top_bits() {
    let mut cpu = Cpu::new();
    cpu.write(0x10, 0xC0);
    cpu.load_and_run(0x8000, &[0xA9, 0x01, 0x24, 0x10, 0x00]).unwrap();
    assert!(cpu.flag(FLAG_Z) && cpu.flag(FLAG_N) && cpu.flag(FLAG_V));
}

#[test]
fn branch_backward_loop_multiplies() {
    let c = run(&[0xA9, 0x00, 0xA2, 0x07, 0x18, 0x69, 0x06, 0xCA, 0xD0, 0xFA, 0x8D, 0x00, 0x02, 0x00]);
    assert_eq!(c.a, 42);
    assert_eq!(c.read(0x0200), 42);
}

#[test]
fn branch_not_taken_falls_through() {
    // LDA #1 ; BEQ +2 (not taken) ; LDX #7 ; BRK
    let c = run(&[0xA9, 0x01, 0xF0, 0x02, 0xA2, 0x07, 0x00]);
    assert_eq!(c.x, 7);
}

#[test]
fn jsr_rts_roundtrip() {
    // 8000: JSR $8006 ; LDX #$05 ; BRK ; 8006: LDA #$77 ; RTS
    let c = run(&[0x20, 0x06, 0x80, 0xA2, 0x05, 0x00, 0xA9, 0x77, 0x60]);
    assert_eq!(c.a, 0x77);
    assert_eq!(c.x, 5);
    assert_eq!(c.sp, 0xFD - 3); // only BRK's push remains
}

#[test]
fn stack_push_pull() {
    let c = run(&[0xA9, 0x42, 0x48, 0xA9, 0x00, 0x68, 0x00]);
    assert_eq!(c.a, 0x42);
}

#[test]
fn php_plp_forces_b_and_u_correctly() {
    let mut cpu = Cpu::new();
    cpu.load_and_run(0x8000, &[0x38, 0x08, 0x18, 0x28, 0x00]).unwrap(); // SEC PHP CLC PLP
    assert!(cpu.flag(FLAG_C));
    assert!(!cpu.flag(FLAG_B));
    assert!(cpu.flag(FLAG_U));
}

#[test]
fn jmp_indirect_page_wrap_bug() {
    let mut cpu = Cpu::new();
    cpu.write(0x02FF, 0x00);
    cpu.write(0x0200, 0x90); // hardware reads the high byte from $0200, not $0300
    cpu.write(0x0300, 0xAA);
    cpu.write(0x9000, 0xA2); // LDX #$33 ; BRK at target
    cpu.write(0x9001, 0x33);
    cpu.write(0x9002, 0x00);
    cpu.load_and_run(0x8000, &[0x6C, 0xFF, 0x02]).unwrap();
    assert_eq!(cpu.x, 0x33);
}

#[test]
fn brk_vectors_through_irq_and_sets_i() {
    let mut cpu = Cpu::new();
    cpu.write(0xFFFE, 0x00);
    cpu.write(0xFFFF, 0x90);
    cpu.load_and_run(0x8000, &[0x58, 0x00]).unwrap(); // CLI ; BRK
    assert_eq!(cpu.pc, 0x9000);
    assert!(cpu.flag(FLAG_I));
    assert_eq!(cpu.read_u16(0x01FC), 0x8003); // return address skips padding byte
}

#[test]
fn rti_restores_status_and_pc() {
    // Hand-build an interrupt frame: return address $8010, status with only carry set, then RTI.
    // Execution resumes at $8010 (zeroed memory = BRK), which ends the run.
    let mut cpu = Cpu::new();
    cpu.load_and_run(0x8000, &[0xA9, 0x80, 0x48, 0xA9, 0x10, 0x48, 0xA9, 0x01, 0x48, 0x40]).unwrap();
    assert!(cpu.flag(FLAG_C));
    // BRK at $8010 pushed return address $8012
    assert_eq!(cpu.read_u16(0x01FC), 0x8012);
}

#[test]
fn unknown_opcode_is_reported() {
    let mut cpu = Cpu::new();
    cpu.load(0x8000, &[0x02]);
    cpu.reset();
    assert_eq!(cpu.step(), Err(StepError::UnknownOpcode { opcode: 0x02, pc: 0x8000 }));
}

#[test]
fn opcode_table_has_151_unique_entries() {
    let mut seen = std::collections::HashSet::new();
    for (b, _, _) in OPCODES {
        assert!(seen.insert(*b), "duplicate opcode {b:02X}");
    }
    assert_eq!(OPCODES.len(), 151);
}

/// Sum 1..=10 with a loop and a subroutine, exercising most of the machinery together.
#[test]
fn integration_sum_one_to_ten() {
    // 8000: LDX #10 ; LDA #0 ; loop: JSR add ; DEX ; BNE loop ; BRK
    // add: STX $10 ; CLC ; ADC $10 ; RTS
    let c = run(&[
        0xA2, 0x0A, 0xA9, 0x00, 0x20, 0x0B, 0x80, 0xCA, 0xD0, 0xFA, 0x00,
        0x86, 0x10, 0x18, 0x65, 0x10, 0x60,
    ]);
    assert_eq!(c.a, 55);
}
