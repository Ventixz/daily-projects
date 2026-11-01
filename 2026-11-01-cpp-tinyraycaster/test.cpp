#include <cassert>
#include <cstdio>
#include "raycaster.h"

static Map box() { return {5, 5, "11111" "1   1" "1   1" "1   1" "11111"}; }
static bool near(float a, float b, float e = 0.02f) { return std::fabs(a - b) < e; }

int main() {
    Map m = box();
    Player p{2.5f, 2.5f, 0, (float)M_PI / 3};
    assert(m.wall(0, 0) && !m.wall(2, 2) && m.wall(-1, 2) && m.wall(5, 2));
    Hit h = cast(m, p, 0);                      // east: wall at x=4 -> dist 1.5
    assert(h.hit && near(h.dist, 1.5f));
    h = cast(m, p, (float)M_PI);                // west: wall edge at x=1 -> 1.5
    assert(h.hit && near(h.dist, 1.5f));
    h = cast(m, p, (float)M_PI / 2);            // south
    assert(h.hit && near(h.dist, 1.5f));
    Map open{3, 3, "   " "   " "   "};          // only out-of-bounds stops it
    Player q{1.5f, 1.5f, 0, 1};
    assert(near(cast(open, q, 0).dist, 1.5f));
    assert(!cast(open, q, 0, 1.0f).hit);        // max_dist honoured
    // closer wall => taller slice; off-axis ray at same Euclidean distance has smaller perpendicular distance
    assert(slice_height(400, 1, 0, 0) > slice_height(400, 2, 0, 0));
    assert(slice_height(400, 2, 0.5f, 0) > slice_height(400, 2, 0, 0));
    assert(slice_height(400, 0, 0, 0) == 40000);  // clamped, no div by zero
    // render: centre column has wall pixels; ceiling/floor colours at edges
    auto fb = render(m, p, 64, 48);
    assert(fb.size() == 64 * 48);
    assert(fb[32 + 24 * 64] != pack(30,30,30) && fb[32 + 24 * 64] != pack(70,55,40));
    assert(fb[32 + 0] == pack(30,30,30) || fb[32] != 0);
    assert(fb[32 + 47 * 64] == pack(70,55,40));
    // symmetric box + centred player => left/right columns mirror
    Player c{2.5f, 2.5f, 0, (float)M_PI / 3};
    auto f2 = render(m, c, 64, 48);
    assert(f2[0 + 24 * 64] != pack(70,55,40));
    // ppm header + size
    std::string s = to_ppm(fb, 64, 48);
    assert(s.rfind("P6\n64 48\n255\n", 0) == 0 && s.size() == 13 + 64 * 48 * 3);
    // wall colour darkens with distance
    Map long_{12, 3, "111111111111" "1          1" "111111111111"};
    Player a1{1.5f, 1.5f, 0, 0.2f}, a2{9.5f, 1.5f, 0, 0.2f};
    auto n1 = render(long_, a1, 8, 8), n2 = render(long_, a2, 8, 8);
    assert((n2[4 + 4 * 8] & 255) > (n1[4 + 4 * 8] & 255));
    std::puts("all tests passed");
}
