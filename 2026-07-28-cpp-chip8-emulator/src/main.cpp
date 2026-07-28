#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

#include <termios.h>
#include <unistd.h>
#include <sys/select.h>

#include "chip8.hpp"

namespace {

// Standard COSMAC VIP keypad, remapped onto a QWERTY block:
//   1 2 3 C            1 2 3 4
//   4 5 6 D    ---->   q w e r
//   7 8 9 E            a s d f
//   A 0 B F            z x c v
constexpr char KEY_LAYOUT[16] = {
    'x', '1', '2', '3', 'q', 'w', 'e', 'a',
    's', 'd', 'z', 'c', '4', 'r', 'f', 'v',
};

int keyIndexFor(char c) {
    for (int i = 0; i < 16; i++) {
        if (KEY_LAYOUT[i] == c) return i;
    }
    return -1;
}

// RAII guard: puts the terminal into raw, non-blocking mode for the
// duration of the emulator's run and restores the previous settings on
// destruction (including on an exception unwinding past main).
class RawTerminal {
public:
    RawTerminal() {
        if (tcgetattr(STDIN_FILENO, &original_) != 0) {
            active_ = false;
            return;
        }
        termios raw = original_;
        raw.c_lflag &= ~(ICANON | ECHO);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
        active_ = true;
    }

    ~RawTerminal() {
        if (active_) tcsetattr(STDIN_FILENO, TCSANOW, &original_);
    }

    RawTerminal(const RawTerminal&) = delete;
    RawTerminal& operator=(const RawTerminal&) = delete;

private:
    termios original_{};
    bool active_ = false;
};

// Non-blocking: returns every character currently waiting on stdin.
// A terminal has no "key up" event, so a key read this frame is only
// held "pressed" for that one frame -- see LEARNING.md for what that
// costs games that need a held-down key (movement, continuous fire).
std::vector<char> pollKeys() {
    std::vector<char> out;
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    timeval timeout{0, 0};
    while (select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &timeout) > 0) {
        char c;
        if (read(STDIN_FILENO, &c, 1) != 1) break;
        out.push_back(c);
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        timeout = {0, 0};
    }
    return out;
}

void render(const chip8::Chip8& cpu) {
    std::string frame;
    frame.reserve((chip8::DISPLAY_WIDTH + 1) * chip8::DISPLAY_HEIGHT + 32);
    frame += "\x1b[H"; // cursor home; avoids a full clear+flicker each frame
    const auto& display = cpu.display();
    for (int y = 0; y < chip8::DISPLAY_HEIGHT; y++) {
        for (int x = 0; x < chip8::DISPLAY_WIDTH; x++) {
            frame += display[static_cast<size_t>(y) * chip8::DISPLAY_WIDTH + x] ? "#" : " ";
        }
        frame += "\n";
    }
    std::fwrite(frame.data(), 1, frame.size(), stdout);
    std::fflush(stdout);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " <rom-file>\n";
        return 1;
    }

    std::ifstream file(argv[1], std::ios::binary);
    if (!file) {
        std::cerr << "could not open ROM: " << argv[1] << "\n";
        return 1;
    }
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    chip8::Chip8 cpu;
    try {
        cpu.loadROM(rom);
    } catch (const std::exception& e) {
        std::cerr << "failed to load ROM: " << e.what() << "\n";
        return 1;
    }

    std::fwrite("\x1b[2J", 1, 4, stdout); // clear once up front

    RawTerminal rawTerminal;
    constexpr int CYCLES_PER_FRAME = 10; // ~600Hz CPU at a 60Hz frame rate
    constexpr auto FRAME_DURATION = std::chrono::milliseconds(1000 / 60);

    bool running = true;
    while (running) {
        auto frameStart = std::chrono::steady_clock::now();

        for (char c : pollKeys()) {
            if (c == 27) { // Esc quits
                running = false;
                break;
            }
            int key = keyIndexFor(c);
            if (key >= 0) cpu.setKey(key, true);
        }

        for (int i = 0; i < CYCLES_PER_FRAME && running; i++) {
            cpu.cycle();
        }
        cpu.tickTimers();

        // Terminal input has no key-up event, so every key we set this
        // frame is released again before the next poll -- see pollKeys().
        for (int k = 0; k < chip8::NUM_KEYS; k++) cpu.setKey(k, false);

        if (cpu.drawFlag()) {
            render(cpu);
            cpu.clearDrawFlag();
        }

        std::this_thread::sleep_until(frameStart + FRAME_DURATION);
    }

    return 0;
}
