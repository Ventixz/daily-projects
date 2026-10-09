// Tetris game logic: no I/O, so it can be unit-tested.
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <random>
#include <vector>

namespace tetris {

constexpr int W = 10;
constexpr int H = 20;

enum Kind { I, O, T, S, Z, J, L, KINDS };

// Each piece is a list of 4 (x, y) cells in a 4x4 box at rotation 0.
using Cells = std::array<std::pair<int, int>, 4>;

inline Cells base_cells(Kind k) {
    switch (k) {
        case I: return {{{0, 1}, {1, 1}, {2, 1}, {3, 1}}};
        case O: return {{{1, 0}, {2, 0}, {1, 1}, {2, 1}}};
        case T: return {{{1, 0}, {0, 1}, {1, 1}, {2, 1}}};
        case S: return {{{1, 0}, {2, 0}, {0, 1}, {1, 1}}};
        case Z: return {{{0, 0}, {1, 0}, {1, 1}, {2, 1}}};
        case J: return {{{0, 0}, {0, 1}, {1, 1}, {2, 1}}};
        default: return {{{2, 0}, {0, 1}, {1, 1}, {2, 1}}};  // L
    }
}

// Rotate clockwise inside the piece's bounding box (size 4 for I, 2-wide O is
// unchanged by design, 3 for the rest). Rotation r is applied r times.
inline Cells rotated(Kind k, int rot) {
    Cells c = base_cells(k);
    if (k == O) return c;
    int n = (k == I) ? 4 : 3;
    for (int r = 0; r < ((rot % 4) + 4) % 4; ++r)
        for (auto& p : c) p = {n - 1 - p.second, p.first};  // (x,y) -> (n-1-y, x)
    return c;
}

struct Piece {
    Kind kind = T;
    int rot = 0;
    int x = 3, y = 0;  // top-left of the bounding box on the board
};

class Game {
public:
    explicit Game(uint32_t seed = 1) : rng_(seed) {
        for (auto& row : grid_) row.fill(-1);
        next_ = draw();
        spawn();
    }

    // -1 empty, else the Kind that locked there.
    int cell(int x, int y) const { return grid_[y][x]; }
    const Piece& piece() const { return cur_; }
    Kind next_kind() const { return next_; }
    int score() const { return score_; }
    int lines() const { return lines_; }
    int level() const { return lines_ / 10; }
    bool over() const { return over_; }

    bool fits(const Piece& p) const {
        for (auto [cx, cy] : rotated(p.kind, p.rot)) {
            int x = p.x + cx, y = p.y + cy;
            if (x < 0 || x >= W || y >= H) return false;
            if (y >= 0 && grid_[y][x] != -1) return false;
        }
        return true;
    }

    bool move(int dx) {
        if (over_) return false;
        Piece p = cur_;
        p.x += dx;
        if (!fits(p)) return false;
        cur_ = p;
        return true;
    }

    // Clockwise rotation with simple wall kicks (try shifting 1 then 2 cells).
    bool rotate() {
        if (over_) return false;
        Piece p = cur_;
        p.rot = (p.rot + 1) % 4;
        for (int kick : {0, -1, 1, -2, 2}) {
            Piece q = p;
            q.x += kick;
            if (fits(q)) { cur_ = q; return true; }
        }
        return false;
    }

    // One gravity step. Returns true if the piece moved, false if it locked.
    bool step() {
        if (over_) return false;
        Piece p = cur_;
        p.y++;
        if (fits(p)) { cur_ = p; return true; }
        lock();
        return false;
    }

    void soft_drop() { if (step()) score_ += 1; }

    void hard_drop() {
        if (over_) return;
        int dist = 0;
        while (true) {
            Piece p = cur_;
            p.y++;
            if (!fits(p)) break;
            cur_ = p;
            ++dist;
        }
        score_ += 2 * dist;
        lock();
    }

    // Row where the current piece would land (for the ghost piece).
    Piece ghost() const {
        Piece g = cur_;
        while (true) {
            Piece p = g;
            p.y++;
            if (!fits(p)) return g;
            g = p;
        }
    }

    // Milliseconds between gravity ticks; speeds up with level, floor 80.
    int gravity_ms() const { return std::max(80, 800 - 70 * level()); }

    // Test hook: fill a cell directly.
    void set_cell(int x, int y, int v) { grid_[y][x] = v; }
    void set_piece(const Piece& p) { cur_ = p; }

private:
    std::array<std::array<int, W>, H> grid_;
    Piece cur_;
    Kind next_;
    std::vector<Kind> bag_;
    std::mt19937 rng_;
    int score_ = 0, lines_ = 0;
    bool over_ = false;

    // 7-bag randomizer: every piece appears once per 7 draws.
    Kind draw() {
        if (bag_.empty()) {
            for (int k = 0; k < KINDS; ++k) bag_.push_back(static_cast<Kind>(k));
            std::shuffle(bag_.begin(), bag_.end(), rng_);
        }
        Kind k = bag_.back();
        bag_.pop_back();
        return k;
    }

    void spawn() {
        cur_ = Piece{next_, 0, 3, 0};
        next_ = draw();
        if (!fits(cur_)) over_ = true;
    }

    void lock() {
        for (auto [cx, cy] : rotated(cur_.kind, cur_.rot)) {
            int x = cur_.x + cx, y = cur_.y + cy;
            if (y >= 0) grid_[y][x] = cur_.kind;
        }
        clear_lines();
        spawn();
    }

    void clear_lines() {
        int cleared = 0;
        for (int y = H - 1; y >= 0;) {
            bool full = std::all_of(grid_[y].begin(), grid_[y].end(),
                                    [](int c) { return c != -1; });
            if (!full) { --y; continue; }
            for (int r = y; r > 0; --r) grid_[r] = grid_[r - 1];
            grid_[0].fill(-1);
            ++cleared;  // re-check the same y: a new row has fallen into it
        }
        static const int pts[] = {0, 100, 300, 500, 800};
        score_ += pts[cleared] * (level() + 1);
        lines_ += cleared;
    }
};

}  // namespace tetris
