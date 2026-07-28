// Hand-rolled test harness -- no GoogleTest/Catch2 to install, matching
// this repo's no-install-needed convention. Each CHECK records a
// pass/fail against a running total; main() prints a summary and exits
// non-zero if anything failed, so `make test` fails the build the way a
// real test runner would.

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "chip8.hpp"

namespace {

int g_passed = 0;
int g_failed = 0;

void check(bool cond, const std::string& name) {
    if (cond) {
        g_passed++;
    } else {
        g_failed++;
        std::printf("FAIL: %s\n", name.c_str());
    }
}

void pushOpcode(std::vector<uint8_t>& rom, uint16_t opcode) {
    rom.push_back(static_cast<uint8_t>(opcode >> 8));
    rom.push_back(static_cast<uint8_t>(opcode & 0xFF));
}

// Loads `opcodes` as a ROM and runs exactly opcodes.size() cycles.
chip8::Chip8 run(const std::vector<uint16_t>& opcodes) {
    chip8::Chip8 cpu;
    std::vector<uint8_t> rom;
    for (uint16_t op : opcodes) pushOpcode(rom, op);
    cpu.loadROM(rom);
    for (size_t i = 0; i < opcodes.size(); i++) cpu.cycle();
    return cpu;
}

void test_load_and_add_immediate() {
    auto cpu = run({0x60AB, 0x7005}); // LD V0, 0xAB ; ADD V0, 0x05
    check(cpu.reg(0) == 0xB0, "6xkk/7xkk: V0 = 0xAB + 0x05 = 0xB0");
}

void test_add_immediate_wraps_without_carry_flag() {
    auto cpu = run({0x60FF, 0x7002}); // LD V0, 0xFF ; ADD V0, 0x02 (wraps, no VF)
    check(cpu.reg(0) == 0x01, "7xkk: ADD Vx, byte wraps mod 256");
    check(cpu.reg(0xF) == 0x00, "7xkk: ADD Vx, byte does not touch VF");
}

void test_add_registers_sets_carry() {
    auto cpu = run({0x60FF, 0x6101, 0x8014}); // V0=0xFF, V1=0x01, ADD V0,V1
    check(cpu.reg(0) == 0x00, "8xy4: V0 wraps to 0x00 on overflow");
    check(cpu.reg(0xF) == 1, "8xy4: VF=1 on carry");

    auto cpu2 = run({0x6001, 0x6101, 0x8014}); // V0=1, V1=1, ADD V0,V1
    check(cpu2.reg(0) == 2, "8xy4: V0 = 1 + 1 = 2");
    check(cpu2.reg(0xF) == 0, "8xy4: VF=0, no carry");
}

void test_sub_sets_not_borrow() {
    auto cpu = run({0x6005, 0x6103, 0x8015}); // V0=5, V1=3, SUB V0,V1
    check(cpu.reg(0) == 2, "8xy5: V0 = 5 - 3 = 2");
    check(cpu.reg(0xF) == 1, "8xy5: VF=1 (no borrow) when Vx >= Vy");

    auto cpu2 = run({0x6003, 0x6105, 0x8015}); // V0=3, V1=5, SUB V0,V1
    check(cpu2.reg(0) == static_cast<uint8_t>(3 - 5), "8xy5: V0 wraps when it borrows");
    check(cpu2.reg(0xF) == 0, "8xy5: VF=0 (borrow) when Vx < Vy");
}

void test_shr_and_shl_capture_shifted_bit() {
    auto cpu = run({0x6003, 0x8006}); // V0 = 0b011, SHR V0
    check(cpu.reg(0) == 1, "8xy6: SHR halves Vx");
    check(cpu.reg(0xF) == 1, "8xy6: VF gets the bit shifted out (LSB)");

    auto cpu2 = run({0x60FF, 0x800E}); // V0 = 0xFF, SHL V0
    check(cpu2.reg(0) == 0xFE, "8xyE: SHL doubles Vx (mod 256)");
    check(cpu2.reg(0xF) == 1, "8xyE: VF gets the bit shifted out (MSB)");
}

void test_jump_and_call_return() {
    // 0x200: JP 0x206 ; 0x202/0x204: unreached LD V0,0x99 (skipped) ;
    // 0x206: LD V1, 0x42
    auto cpu = run({0x1206, 0x6099, 0x0000, 0x6142});
    check(cpu.reg(0) == 0, "1nnn: JP addr skips the instructions in between");
    check(cpu.reg(1) == 0x42, "1nnn: JP addr lands exactly on the target");

    // CALL a subroutine that sets V0, then RET back to fall through to
    // an instruction that sets V1 -- proves the stack round-trips pc.
    chip8::Chip8 cpu2;
    std::vector<uint8_t> rom;
    pushOpcode(rom, 0x2206); // 0x200: CALL 0x206
    pushOpcode(rom, 0x6155); // 0x202: LD V1, 0x55 (runs after RET)
    pushOpcode(rom, 0x1204); // 0x204: JP 0x204 (halt)
    pushOpcode(rom, 0x60AA); // 0x206: LD V0, 0xAA
    pushOpcode(rom, 0x00EE); // 0x208: RET
    cpu2.loadROM(rom);
    check(cpu2.stackPointer() == 0, "2nnn/00EE: sp starts at 0");
    cpu2.cycle(); // CALL
    check(cpu2.stackPointer() == 1, "2nnn: CALL pushes the return address");
    check(cpu2.programCounter() == 0x206, "2nnn: CALL jumps to the subroutine");
    cpu2.cycle(); // LD V0, 0xAA
    cpu2.cycle(); // RET
    check(cpu2.stackPointer() == 0, "00EE: RET pops the stack back to 0");
    check(cpu2.programCounter() == 0x202, "00EE: RET resumes right after CALL");
    cpu2.cycle(); // LD V1, 0x55
    check(cpu2.reg(0) == 0xAA && cpu2.reg(1) == 0x55, "CALL/RET: both sides of the call ran");
}

void test_skip_instructions() {
    // SE Vx, byte: true case skips, false case falls through.
    auto skipTrue = run({0x6042, 0x3042, 0x6199, 0x0000}); // V0=0x42, SE V0,0x42 (skip), LD V1,0x99
    check(skipTrue.reg(1) == 0, "3xkk: SE skips the next instruction when equal");

    auto skipFalse = run({0x6042, 0x3043, 0x6199}); // V0=0x42, SE V0,0x43 (no skip), LD V1,0x99
    check(skipFalse.reg(1) == 0x99, "3xkk: SE falls through when not equal");

    auto sne = run({0x6042, 0x4043, 0x6199, 0x0000}); // SNE Vx,byte: unequal -> skip
    check(sne.reg(1) == 0, "4xkk: SNE skips when Vx != byte");

    auto se_reg = run({0x6005, 0x6105, 0x5010, 0x6299, 0x0000}); // V0==V1 -> skip
    check(se_reg.reg(2) == 0, "5xy0: SE Vx,Vy skips when registers are equal");

    auto sne_reg = run({0x6005, 0x6106, 0x9010, 0x6299, 0x0000}); // V0!=V1 -> skip
    check(sne_reg.reg(2) == 0, "9xy0: SNE Vx,Vy skips when registers differ");
}

void test_index_register_and_jp_v0() {
    auto cpu = run({0xA123}); // LD I, 0x123
    check(cpu.indexRegister() == 0x123, "Annn: LD I, addr");

    auto cpu2 = run({0x6010, 0xB200}); // V0=0x10, JP V0, 0x200 -> pc = 0x210
    check(cpu2.programCounter() == 0x210, "Bnnn: JP V0, addr adds V0 to the target");
}

void test_bcd_conversion() {
    auto cpu = run({0x607B, 0xA300, 0xF033}); // V0 = 123, I = 0x300, BCD
    check(cpu.memoryAt(0x300) == 1, "Fx33: hundreds digit");
    check(cpu.memoryAt(0x301) == 2, "Fx33: tens digit");
    check(cpu.memoryAt(0x302) == 3, "Fx33: ones digit");

    auto cpu2 = run({0x6007, 0xA300, 0xF033}); // V0 = 7
    check(cpu2.memoryAt(0x300) == 0 && cpu2.memoryAt(0x301) == 0 && cpu2.memoryAt(0x302) == 7,
          "Fx33: single-digit values zero-pad the higher digits");
}

void test_register_store_and_load_roundtrip() {
    auto cpu = run({
        0x6011, 0x6122, 0x6233, // V0=0x11, V1=0x22, V2=0x33
        0xA400,                 // I = 0x400
        0xF255,                 // store V0..V2 at [I..I+2]
    });
    check(cpu.memoryAt(0x400) == 0x11 && cpu.memoryAt(0x401) == 0x22 && cpu.memoryAt(0x402) == 0x33,
          "Fx55: LD [I], Vx stores V0..Vx in order");

    // Store, clear the registers, then load them back from the same memory
    // to prove the pair genuinely round-trips rather than just mirroring
    // whatever V0..Vx already held.
    auto full = run({
        0x6011, 0x6122, 0x6233, // V0=0x11, V1=0x22, V2=0x33
        0xA400, 0xF255,         // store V0..V2 at 0x400..0x402
        0x6000, 0x6100, 0x6200, // clear V0..V2
        0xA400, 0xF265,         // load V0..V2 back from 0x400..0x402
    });
    check(full.reg(0) == 0x11 && full.reg(1) == 0x22 && full.reg(2) == 0x33,
          "Fx55/Fx65: store then load round-trips V0..Vx exactly");
}

void test_font_sprite_address() {
    auto cpu = run({0x6003, 0xF029}); // V0 = 3, LD F, V0
    check(cpu.indexRegister() == chip8::FONT_START + 3 * chip8::FONT_BYTES_PER_DIGIT,
          "Fx29: LD F, Vx points I at digit Vx's 5-byte glyph");
}

void test_draw_and_collision() {
    // Point I at font digit 0's glyph, draw it at (0,0), then draw it
    // again at the same spot: the second draw must report a collision
    // and XOR every lit pixel back off (CHIP-8's own "erase sprite" idiom).
    chip8::Chip8 cpu;
    std::vector<uint8_t> rom;
    pushOpcode(rom, 0x6000); // V0 = 0 (x)
    pushOpcode(rom, 0x6100); // V1 = 0 (y)
    pushOpcode(rom, 0xF029); // I = font digit 0
    pushOpcode(rom, 0xD015); // DRW V0, V1, 5
    cpu.loadROM(rom);
    cpu.cycle();
    cpu.cycle();
    cpu.cycle();
    cpu.cycle(); // first draw

    const auto& disp = cpu.display();
    // Digit 0's glyph: 0xF0,0x90,0x90,0x90,0xF0 -> rows of "1111","1001",...
    bool rowsMatch =
        disp[0] == 1 && disp[1] == 1 && disp[2] == 1 && disp[3] == 1 &&                    // row0: 1111
        disp[chip8::DISPLAY_WIDTH + 0] == 1 && disp[chip8::DISPLAY_WIDTH + 1] == 0 &&
        disp[chip8::DISPLAY_WIDTH + 2] == 0 && disp[chip8::DISPLAY_WIDTH + 3] == 1;         // row1: 1001
    check(rowsMatch, "Dxyn: draws the font glyph's exact bit pattern into the display buffer");
    check(cpu.reg(0xF) == 0, "Dxyn: VF=0 on a draw with no prior collision");

    // A second, independent program that draws the same sprite twice in a
    // row, to check the collision/erase behavior on the redraw.
    chip8::Chip8 cpu2;
    std::vector<uint8_t> rom2;
    pushOpcode(rom2, 0x6000);
    pushOpcode(rom2, 0x6100);
    pushOpcode(rom2, 0xF029);
    pushOpcode(rom2, 0xD015); // draw once
    pushOpcode(rom2, 0xD015); // draw again, same I/x/y -> collision + erase
    cpu2.loadROM(rom2);
    for (int i = 0; i < 5; i++) cpu2.cycle();
    check(cpu2.reg(0xF) == 1, "Dxyn: VF=1 when redrawing onto already-lit pixels");
    bool erased = true;
    for (int i = 0; i < chip8::DISPLAY_WIDTH * chip8::DISPLAY_HEIGHT; i++) {
        if (cpu2.display()[static_cast<size_t>(i)] != 0) erased = false;
    }
    check(erased, "Dxyn: drawing the same sprite twice XORs it fully back off");
}

void test_cls_clears_display() {
    chip8::Chip8 cpu;
    std::vector<uint8_t> rom;
    pushOpcode(rom, 0x6000);
    pushOpcode(rom, 0x6100);
    pushOpcode(rom, 0xF029);
    pushOpcode(rom, 0xD015); // draw digit 0
    pushOpcode(rom, 0x00E0); // CLS
    cpu.loadROM(rom);
    for (int i = 0; i < 5; i++) cpu.cycle();
    bool allClear = true;
    for (int i = 0; i < chip8::DISPLAY_WIDTH * chip8::DISPLAY_HEIGHT; i++) {
        if (cpu.display()[static_cast<size_t>(i)] != 0) allClear = false;
    }
    check(allClear, "00E0: CLS zeroes every pixel");
}

void test_timers_count_down_independently_of_cycles() {
    chip8::Chip8 cpu;
    std::vector<uint8_t> rom;
    pushOpcode(rom, 0x6005);
    pushOpcode(rom, 0xF015); // DT = 5
    pushOpcode(rom, 0x6005);
    pushOpcode(rom, 0xF018); // ST = 5
    cpu.loadROM(rom);
    cpu.cycle();
    cpu.cycle();
    check(cpu.delayTimer() == 5, "Fx15: LD DT, Vx sets the delay timer");
    cpu.cycle();
    cpu.cycle();
    check(cpu.soundTimer() == 5, "Fx18: LD ST, Vx sets the sound timer");
    check(cpu.soundActive(), "soundActive() is true while ST > 0");
    for (int i = 0; i < 5; i++) cpu.tickTimers();
    check(cpu.delayTimer() == 0 && cpu.soundTimer() == 0, "tickTimers: both timers count down to 0");
    cpu.tickTimers();
    check(cpu.delayTimer() == 0, "tickTimers: delay timer does not underflow past 0");
    check(!cpu.soundActive(), "soundActive() is false once ST reaches 0");
}

void test_key_wait_blocks_until_pressed() {
    chip8::Chip8 cpu;
    std::vector<uint8_t> rom;
    pushOpcode(rom, 0xF00A); // LD V0, K
    cpu.loadROM(rom);
    cpu.cycle();
    check(cpu.programCounter() == chip8::PROGRAM_START, "Fx0A: re-runs the same instruction while no key is down");
    cpu.cycle();
    check(cpu.programCounter() == chip8::PROGRAM_START, "Fx0A: still blocked on the second cycle");
    cpu.setKey(7, true);
    cpu.cycle();
    check(cpu.reg(0) == 7, "Fx0A: captures the pressed key's index once one is down");
    check(cpu.programCounter() == chip8::PROGRAM_START + 2, "Fx0A: advances past the instruction once unblocked");
}

void test_skp_and_sknp() {
    chip8::Chip8 cpu;
    std::vector<uint8_t> rom;
    pushOpcode(rom, 0x6005); // V0 = 5
    pushOpcode(rom, 0xE09E); // SKP V0 (key 5) -- key not down, no skip
    pushOpcode(rom, 0x6199); // this runs
    cpu.loadROM(rom);
    for (int i = 0; i < 3; i++) cpu.cycle();
    check(cpu.reg(1) == 0x99, "Ex9E: SKP does not skip when the key is up");

    chip8::Chip8 cpu2;
    std::vector<uint8_t> rom2;
    pushOpcode(rom2, 0x6005); // V0 = 5
    pushOpcode(rom2, 0xE0A1); // SKNP V0 -- key not down, should skip
    pushOpcode(rom2, 0x6199); // skipped
    cpu2.loadROM(rom2);
    for (int i = 0; i < 3; i++) cpu2.cycle();
    check(cpu2.reg(1) == 0, "ExA1: SKNP skips when the key is up");
}

void test_add_i_and_rng_seed_is_deterministic() {
    auto cpu = run({0xA010, 0x600F, 0xF01E}); // I=0x10, V0=0xF, ADD I, V0
    check(cpu.indexRegister() == 0x1F, "Fx1E: ADD I, Vx");

    chip8::Chip8 a;
    chip8::Chip8 b;
    a.seedRandom(42);
    b.seedRandom(42);
    std::vector<uint8_t> rom;
    pushOpcode(rom, 0xC0FF); // RND V0, 0xFF
    a.loadROM(rom);
    b.loadROM(rom);
    a.cycle();
    b.cycle();
    check(a.reg(0) == b.reg(0), "seedRandom: identical seeds reproduce identical RND output");

    auto masked = run({0xC000}); // RND V0, 0x00 -- mask forces the result to 0 regardless of the draw
    check(masked.reg(0) == 0, "Cxkk: RND ANDs the drawn byte with kk");
}

} // namespace

int main() {
    test_load_and_add_immediate();
    test_add_immediate_wraps_without_carry_flag();
    test_add_registers_sets_carry();
    test_sub_sets_not_borrow();
    test_shr_and_shl_capture_shifted_bit();
    test_jump_and_call_return();
    test_skip_instructions();
    test_index_register_and_jp_v0();
    test_bcd_conversion();
    test_register_store_and_load_roundtrip();
    test_font_sprite_address();
    test_draw_and_collision();
    test_cls_clears_display();
    test_timers_count_down_independently_of_cycles();
    test_key_wait_blocks_until_pressed();
    test_skp_and_sknp();
    test_add_i_and_rng_seed_is_deterministic();

    std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
