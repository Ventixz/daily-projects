use nes_cpu::Cpu;

// Classic 6502 demo: sum 1..=10 into A, store at $0200.
fn main() {
    let program = [
        0xA2, 0x0A, // LDX #10
        0xA9, 0x00, // LDA #0
        0x18, //       CLC
        0x86, 0x10, // STX $10
        0x65, 0x10, // ADC $10
        0xCA, //       DEX
        0xD0, 0xF9, // BNE -7 (back to STX)
        0x8D, 0x00, 0x02, // STA $0200
        0x00, //       BRK
    ];
    let mut cpu = Cpu::new();
    cpu.load_and_run(&program);
    println!("A = {}, mem[$0200] = {}", cpu.a, cpu.read(0x0200));
}
