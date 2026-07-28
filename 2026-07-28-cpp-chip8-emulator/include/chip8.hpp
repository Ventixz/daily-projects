#pragma once

#include <array>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <vector>

namespace chip8 {

constexpr int MEMORY_SIZE = 4096;
constexpr int NUM_REGISTERS = 16;
constexpr int STACK_SIZE = 16;
constexpr int DISPLAY_WIDTH = 64;
constexpr int DISPLAY_HEIGHT = 32;
constexpr int NUM_KEYS = 16;
constexpr uint16_t PROGRAM_START = 0x200;
constexpr uint16_t FONT_START = 0x50;
constexpr int FONT_BYTES_PER_DIGIT = 5;

// The CHIP-8 CPU: memory, registers, stack, timers and the monochrome
// display buffer, plus one fetch-decode-execute step (cycle()).
//
// This class knows nothing about terminals, files, or wall-clock time --
// that's main.cpp's job. Keeping I/O out of here is what lets the tests
// drive the interpreter directly, one instruction at a time, without a
// terminal or a real ROM file.
class Chip8 {
public:
    Chip8();

    void reset();
    void loadROM(const std::vector<uint8_t>& data);

    // Fetch the instruction at pc, decode it, execute it. Advances pc
    // (either past the instruction, or to a jump/call target).
    void cycle();

    // Decrement delay/sound timers by one. The caller is responsible for
    // calling this at 60Hz -- the interpreter has no clock of its own.
    void tickTimers();

    bool drawFlag() const { return drawFlag_; }
    void clearDrawFlag() { drawFlag_ = false; }
    const std::array<uint8_t, DISPLAY_WIDTH * DISPLAY_HEIGHT>& display() const {
        return display_;
    }

    void setKey(int key, bool pressed);
    bool soundActive() const { return soundTimer_ > 0; }

    // Test/debug accessors.
    uint8_t reg(int i) const { return V_[static_cast<size_t>(i)]; }
    uint16_t indexRegister() const { return I_; }
    uint16_t programCounter() const { return pc_; }
    uint8_t stackPointer() const { return sp_; }
    uint8_t memoryAt(uint16_t addr) const { return memory_[addr]; }
    uint8_t delayTimer() const { return delayTimer_; }
    uint8_t soundTimer() const { return soundTimer_; }

    // Seeding is exposed so tests can make the RND opcode reproducible.
    void seedRandom(uint32_t seed) { rng_.seed(seed); }

private:
    std::array<uint8_t, MEMORY_SIZE> memory_{};
    std::array<uint8_t, NUM_REGISTERS> V_{};
    uint16_t I_ = 0;
    uint16_t pc_ = PROGRAM_START;
    std::array<uint16_t, STACK_SIZE> stack_{};
    uint8_t sp_ = 0;
    uint8_t delayTimer_ = 0;
    uint8_t soundTimer_ = 0;
    std::array<uint8_t, DISPLAY_WIDTH * DISPLAY_HEIGHT> display_{};
    std::array<bool, NUM_KEYS> keys_{};
    bool drawFlag_ = true;
    std::mt19937 rng_{std::random_device{}()};
    std::uniform_int_distribution<int> byteDist_{0, 255};

    void execute(uint16_t opcode);
};

} // namespace chip8
