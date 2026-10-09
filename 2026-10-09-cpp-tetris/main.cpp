// Terminal Tetris. Keys: a/d or arrows move, w/up rotate, s/down soft drop,
// space hard drop, q quit.
#include <termios.h>
#include <unistd.h>
#include <poll.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include "tetris.hpp"

using namespace tetris;

static termios saved;
static void restore() {
    tcsetattr(STDIN_FILENO, TCSANOW, &saved);
    std::printf("\x1b[?25h\x1b[0m\n");
}
static void raw() {
    tcgetattr(STDIN_FILENO, &saved);
    atexit(restore);
    termios t = saved;
    t.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
    std::printf("\x1b[?25l");
}

static void draw(const Game& g) {
    static const char* colors[] = {"36", "33", "35", "32", "31", "34", "91"};
    int board[H][W];
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) board[y][x] = g.cell(x, y);
    // overlay ghost (as -2) then the live piece
    Piece gh = g.ghost();
    for (auto [cx, cy] : rotated(gh.kind, gh.rot))
        if (gh.y + cy >= 0) board[gh.y + cy][gh.x + cx] = -2;
    const Piece& p = g.piece();
    for (auto [cx, cy] : rotated(p.kind, p.rot))
        if (p.y + cy >= 0) board[p.y + cy][p.x + cx] = p.kind;

    std::string out = "\x1b[H";
    for (int y = 0; y < H; ++y) {
        out += "|";
        for (int x = 0; x < W; ++x) {
            int c = board[y][x];
            if (c == -1) out += " .";
            else if (c == -2) out += "\x1b[90m[]\x1b[0m";
            else out += std::string("\x1b[") + colors[c] + "m[]\x1b[0m";
        }
        out += "|";
        if (y == 1) out += "  Score " + std::to_string(g.score());
        if (y == 2) out += "  Lines " + std::to_string(g.lines());
        if (y == 3) out += "  Level " + std::to_string(g.level());
        if (y == 5) out += "  Next:";
        if (y >= 6 && y < 8)
            for (int x = 0; x < 4; ++x) {
                bool on = false;
                for (auto [cx, cy] : rotated(g.next_kind(), 0)) on |= (cx == x && cy == y - 6);
                if (x == 0) out += "  ";
                out += on ? "[]" : "  ";
            }
        out += "\x1b[K\n";
    }
    out += "+" + std::string(W * 2, '-') + "+\n";
    if (g.over()) out += "GAME OVER\x1b[K\n";
    std::fputs(out.c_str(), stdout);
    std::fflush(stdout);
}

int main() {
    if (!isatty(STDIN_FILENO)) { std::fputs("needs a terminal\n", stderr); return 1; }
    raw();
    std::printf("\x1b[2J");
    Game g(static_cast<uint32_t>(std::time(nullptr)));
    using clk = std::chrono::steady_clock;
    auto last = clk::now();
    draw(g);
    while (!g.over()) {
        auto now = clk::now();
        int wait = g.gravity_ms() - (int)std::chrono::duration_cast<std::chrono::milliseconds>(now - last).count();
        if (wait < 0) wait = 0;
        pollfd pf{STDIN_FILENO, POLLIN, 0};
        if (poll(&pf, 1, wait) > 0) {
            char ch;
            if (read(STDIN_FILENO, &ch, 1) == 1) {
                if (ch == 'q') break;
                if (ch == 27) {  // arrow keys: ESC [ A/B/C/D
                    char seq[2];
                    if (read(STDIN_FILENO, &seq[0], 1) == 1 && read(STDIN_FILENO, &seq[1], 1) == 1 && seq[0] == '[')
                        ch = seq[1] == 'A' ? 'w' : seq[1] == 'B' ? 's' : seq[1] == 'C' ? 'd' : seq[1] == 'D' ? 'a' : 0;
                }
                switch (ch) {
                    case 'a': g.move(-1); break;
                    case 'd': g.move(1); break;
                    case 'w': g.rotate(); break;
                    case 's': g.soft_drop(); break;
                    case ' ': g.hard_drop(); break;
                }
                draw(g);
            }
        }
        if (clk::now() - last >= std::chrono::milliseconds(g.gravity_ms())) {
            g.step();
            last = clk::now();
            draw(g);
        }
    }
    draw(g);
    return 0;
}
