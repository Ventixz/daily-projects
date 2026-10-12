use nes6502::Cpu;

fn main() {
    // Multiply 6 * 7 by repeated addition, then store the result at $0200.
    //   LDA #$00 ; LDX #$07 ; loop: CLC ; ADC #$06 ; DEX ; BNE loop ; STA $0200 ; BRK
    let program = [0xA9, 0x00, 0xA2, 0x07, 0x18, 0x69, 0x06, 0xCA, 0xD0, 0xFA, 0x8D, 0x00, 0x02, 0x00];
    let mut cpu = Cpu::new();
    let steps = cpu.load_and_run(0x8000, &program).expect("program should run");
    println!("ran {steps} instructions");
    println!("6 * 7 = {} (stored at $0200: {})", cpu.a, cpu.read(0x0200));
    println!("A={:02X} X={:02X} Y={:02X} SP={:02X} PC={:04X} P={:08b}", cpu.a, cpu.x, cpu.y, cpu.sp, cpu.pc, cpu.status);
}
