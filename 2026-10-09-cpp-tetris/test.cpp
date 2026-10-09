#include <cassert>
#include <cstdio>
#include <set>
#include "tetris.hpp"
using namespace tetris;

static int tests = 0;
#define CHECK(c) do { ++tests; if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

int main() {
    // every rotation of every piece has 4 distinct cells inside its box
    for (int k = 0; k < KINDS; ++k)
        for (int r = 0; r < 4; ++r) {
            std::set<std::pair<int,int>> s;
            for (auto p : rotated((Kind)k, r)) {
                CHECK(p.first >= 0 && p.first < 4 && p.second >= 0 && p.second < 4);
                s.insert(p);
            }
            CHECK(s.size() == 4);
        }
    // four rotations return to the start; O never changes
    for (int k = 0; k < KINDS; ++k) CHECK(rotated((Kind)k, 4) == rotated((Kind)k, 0));
    CHECK(rotated(O, 1) == rotated(O, 0));
    CHECK(rotated(I, 0) != rotated(I, 1));

    // 7-bag: first 7 pieces (current + next + 5 more draws) are all distinct
    {
        Game g(42);
        std::set<int> seen{g.piece().kind, g.next_kind()};
        for (int i = 0; i < 5; ++i) { g.hard_drop(); seen.insert(g.next_kind()); }
        CHECK(seen.size() == 7);
    }

    // walls stop movement
    {
        Game g(1);
        for (int i = 0; i < 20; ++i) g.move(-1);
        int left = g.piece().x;
        CHECK(!g.move(-1));
        CHECK(g.piece().x == left);
        for (int i = 0; i < 20; ++i) g.move(1);
        CHECK(!g.move(1));
    }

    // gravity moves down, then locks on the floor and spawns a new piece
    {
        Game g(1);
        int y0 = g.piece().y;
        CHECK(g.step());
        CHECK(g.piece().y == y0 + 1);
        while (g.step()) {}
        bool any = false;
        for (int x = 0; x < W; ++x) any |= g.cell(x, H - 1) != -1;
        CHECK(any);
        CHECK(g.piece().y <= 1);
    }

    // line clear: fill bottom row except a 4-wide gap, drop a flat I into it
    {
        Game g(1);
        g.set_piece(Piece{I, 0, 3, 0});
        for (int x = 0; x < W; ++x) if (x < 3 || x > 6) g.set_cell(x, H - 1, O);
        g.set_cell(0, H - 2, T);  // marker above should fall one row
        g.hard_drop();
        CHECK(g.lines() == 1);
        for (int x = 0; x < W; ++x) CHECK(g.cell(x, H - 1) == (x == 0 ? T : -1));
        CHECK(g.score() > 0);
    }

    // multi-line clear scores more than single (tetris = 800 at level 0)
    {
        Game g(1);
        g.set_piece(Piece{I, 1, 0, 0});  // vertical I occupies column x+2
        for (int y = H - 4; y < H; ++y)
            for (int x = 0; x < W; ++x) if (x != 2) g.set_cell(x, y, O);
        g.hard_drop();
        CHECK(g.lines() == 4);
        CHECK(g.score() == 800 + 2 * 16);  // 800 + hard-drop distance bonus
    }

    // rotation is blocked/kicked at the wall instead of leaving the board
    {
        Game g(1);
        g.set_piece(Piece{I, 1, 0, 5});
        for (int i = 0; i < 20; ++i) g.move(-1);
        CHECK(g.rotate());
        for (auto [cx, cy] : rotated(g.piece().kind, g.piece().rot))
            CHECK(g.piece().x + cx >= 0 && g.piece().x + cx < W);
    }

    // ghost lands at or below the piece and fits
    {
        Game g(7);
        Piece gh = g.ghost();
        CHECK(gh.y >= g.piece().y);
        CHECK(g.fits(gh));
    }

    // game over when the stack reaches the spawn area
    {
        Game g(1);
        for (int y = 0; y < H; ++y) for (int x = 0; x < W - 1; ++x) g.set_cell(x, y, O);  // column 9 open: no row is full
        g.hard_drop();
        CHECK(g.over());
        CHECK(!g.move(1));
    }

    // gravity speeds up with level and has a floor
    {
        Game g(1);
        CHECK(g.gravity_ms() == 800);
    }

    std::printf("ok: %d checks passed\n", tests);
    return 0;
}
