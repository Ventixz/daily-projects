#include "chip8.hpp"

#include <cstring>

namespace chip8 {

namespace {

// The de-facto standard CHIP-8 font: 16 hex digits, 5 bytes each, one bit
// per pixel across a 4-pixel-wide glyph. Interpreters conventionally place
// this in low memory (0x000-0x1FF, here at FONT_START) since that region is
// otherwise unused by ROMs, and Fx29 expects to find it there.
constexpr std::array<uint8_t, 16 * FONT_BYTES_PER_DIGIT> FONT_SET = {{
    0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
    0x20, 0x60, 0x20, 0x20, 0x70, // 1
    0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
    0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
    0x90, 0x90, 0xF0, 0x10, 0x10, // 4
    0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
    0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
    0xF0, 0x10, 0x20, 0x40, 0x40, // 7
    0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
    0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
    0xF0, 0x90, 0xF0, 0x90, 0x90, // A
    0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
    0xF0, 0x80, 0x80, 0x80, 0xF0, // C
    0xE0, 0x90, 0x90, 0x90, 0xE0, // D
    0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
    0xF0, 0x80, 0xF0, 0x80, 0x80, // F
}};

} // namespace

Chip8::Chip8() {
    reset();
}

void Chip8::reset() {
    memory_.fill(0);
    V_.fill(0);
    I_ = 0;
    pc_ = PROGRAM_START;
    stack_.fill(0);
    sp_ = 0;
    delayTimer_ = 0;
    soundTimer_ = 0;
    display_.fill(0);
    keys_.fill(false);
    drawFlag_ = true;
    std::memcpy(memory_.data() + FONT_START, FONT_SET.data(), FONT_SET.size());
}

void Chip8::loadROM(const std::vector<uint8_t>& data) {
    if (data.size() > MEMORY_SIZE - PROGRAM_START) {
        throw std::runtime_error("ROM too large for CHIP-8 memory");
    }
    std::memcpy(memory_.data() + PROGRAM_START, data.data(), data.size());
}

void Chip8::setKey(int key, bool pressed) {
    if (key < 0 || key >= NUM_KEYS) {
        throw std::out_of_range("CHIP-8 key index must be 0-15");
    }
    keys_[static_cast<size_t>(key)] = pressed;
}

void Chip8::tickTimers() {
    if (delayTimer_ > 0) delayTimer_--;
    if (soundTimer_ > 0) soundTimer_--;
}

void Chip8::cycle() {
    uint16_t opcode = static_cast<uint16_t>((memory_[pc_] << 8) | memory_[pc_ + 1]);
    pc_ += 2;
    execute(opcode);
}

void Chip8::execute(uint16_t opcode) {
    const uint8_t x = (opcode & 0x0F00) >> 8;
    const uint8_t y = (opcode & 0x00F0) >> 4;
    const uint8_t n = opcode & 0x000F;
    const uint8_t kk = opcode & 0x00FF;
    const uint16_t nnn = opcode & 0x0FFF;

    switch (opcode & 0xF000) {
    case 0x0000:
        if (opcode == 0x00E0) { // CLS
            display_.fill(0);
            drawFlag_ = true;
        } else if (opcode == 0x00EE) { // RET
            if (sp_ == 0) throw std::runtime_error("stack underflow on RET");
            sp_--;
            pc_ = stack_[sp_];
        }
        // Other 0nnn (SYS addr) opcodes are legacy machine-code calls,
        // ignored by every modern interpreter -- intentionally a no-op.
        break;

    case 0x1000: // JP addr
        pc_ = nnn;
        break;

    case 0x2000: // CALL addr
        if (sp_ >= STACK_SIZE) throw std::runtime_error("stack overflow on CALL");
        stack_[sp_] = pc_;
        sp_++;
        pc_ = nnn;
        break;

    case 0x3000: // SE Vx, byte
        if (V_[x] == kk) pc_ += 2;
        break;

    case 0x4000: // SNE Vx, byte
        if (V_[x] != kk) pc_ += 2;
        break;

    case 0x5000: // SE Vx, Vy
        if (n == 0 && V_[x] == V_[y]) pc_ += 2;
        break;

    case 0x6000: // LD Vx, byte
        V_[x] = kk;
        break;

    case 0x7000: // ADD Vx, byte (no carry flag -- unlike 8xy4)
        V_[x] = static_cast<uint8_t>(V_[x] + kk);
        break;

    case 0x8000:
        switch (n) {
        case 0x0: V_[x] = V_[y]; break;
        case 0x1: V_[x] |= V_[y]; break;
        case 0x2: V_[x] &= V_[y]; break;
        case 0x3: V_[x] ^= V_[y]; break;
        case 0x4: { // ADD Vx, Vy -- VF = carry
            uint16_t sum = static_cast<uint16_t>(V_[x]) + V_[y];
            V_[0xF] = sum > 0xFF ? 1 : 0;
            V_[x] = static_cast<uint8_t>(sum);
            break;
        }
        case 0x5: // SUB Vx, Vy -- VF = NOT borrow
            V_[0xF] = V_[x] >= V_[y] ? 1 : 0;
            V_[x] = static_cast<uint8_t>(V_[x] - V_[y]);
            break;
        case 0x6: { // SHR Vx -- VF = the bit shifted out
            uint8_t shiftedOut = V_[x] & 0x1;
            V_[x] = static_cast<uint8_t>(V_[x] >> 1);
            V_[0xF] = shiftedOut;
            break;
        }
        case 0x7: // SUBN Vx, Vy -- Vx = Vy - Vx, VF = NOT borrow
            V_[0xF] = V_[y] >= V_[x] ? 1 : 0;
            V_[x] = static_cast<uint8_t>(V_[y] - V_[x]);
            break;
        case 0xE: { // SHL Vx -- VF = the bit shifted out
            uint8_t shiftedOut = (V_[x] & 0x80) >> 7;
            V_[x] = static_cast<uint8_t>(V_[x] << 1);
            V_[0xF] = shiftedOut;
            break;
        }
        default:
            break;
        }
        break;

    case 0x9000: // SNE Vx, Vy
        if (n == 0 && V_[x] != V_[y]) pc_ += 2;
        break;

    case 0xA000: // LD I, addr
        I_ = nnn;
        break;

    case 0xB000: // JP V0, addr
        pc_ = static_cast<uint16_t>(nnn + V_[0]);
        break;

    case 0xC000: // RND Vx, byte
        V_[x] = static_cast<uint8_t>(byteDist_(rng_) & kk);
        break;

    case 0xD000: { // DRW Vx, Vy, nibble
        uint8_t startX = V_[x] % DISPLAY_WIDTH;
        uint8_t startY = V_[y] % DISPLAY_HEIGHT;
        V_[0xF] = 0;
        for (int row = 0; row < n; row++) {
            uint8_t spriteByte = memory_[I_ + row];
            int py = startY + row;
            if (py >= DISPLAY_HEIGHT) break; // clip, no wrap
            for (int col = 0; col < 8; col++) {
                int px = startX + col;
                if (px >= DISPLAY_WIDTH) break; // clip, no wrap
                uint8_t spritePixel = (spriteByte >> (7 - col)) & 0x1;
                if (spritePixel == 0) continue;
                size_t idx = static_cast<size_t>(py) * DISPLAY_WIDTH + px;
                if (display_[idx] == 1) V_[0xF] = 1; // collision
                display_[idx] ^= 1;
            }
        }
        drawFlag_ = true;
        break;
    }

    case 0xE000:
        if (kk == 0x9E) { // SKP Vx
            if (keys_[V_[x] & 0xF]) pc_ += 2;
        } else if (kk == 0xA1) { // SKNP Vx
            if (!keys_[V_[x] & 0xF]) pc_ += 2;
        }
        break;

    case 0xF000:
        switch (kk) {
        case 0x07: V_[x] = delayTimer_; break;
        case 0x0A: { // LD Vx, K -- block until a key is down
            bool found = false;
            for (int k = 0; k < NUM_KEYS; k++) {
                if (keys_[static_cast<size_t>(k)]) {
                    V_[x] = static_cast<uint8_t>(k);
                    found = true;
                    break;
                }
            }
            if (!found) pc_ -= 2; // re-run this instruction next cycle
            break;
        }
        case 0x15: delayTimer_ = V_[x]; break;
        case 0x18: soundTimer_ = V_[x]; break;
        case 0x1E: I_ = static_cast<uint16_t>(I_ + V_[x]); break;
        case 0x29: I_ = static_cast<uint16_t>(FONT_START + (V_[x] & 0xF) * FONT_BYTES_PER_DIGIT); break;
        case 0x33: // LD B, Vx -- BCD of Vx into memory[I..I+2]
            memory_[I_] = V_[x] / 100;
            memory_[I_ + 1] = (V_[x] / 10) % 10;
            memory_[I_ + 2] = V_[x] % 10;
            break;
        case 0x55: // LD [I], Vx -- store V0..Vx into memory starting at I
            for (int i = 0; i <= x; i++) memory_[I_ + i] = V_[static_cast<size_t>(i)];
            break;
        case 0x65: // LD Vx, [I] -- load V0..Vx from memory starting at I
            for (int i = 0; i <= x; i++) V_[static_cast<size_t>(i)] = memory_[I_ + i];
            break;
        default:
            break;
        }
        break;

    default:
        break;
    }
}

} // namespace chip8
