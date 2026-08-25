// Hand-rolled test harness -- no GoogleTest/Catch2 install, matching this
// repo's no-install-needed convention (see 2026-07-28-cpp-chip8-emulator,
// 2026-08-08-cpp-raytracer).

#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>

#include "geometry.hpp"
#include "image.hpp"
#include "model.hpp"
#include "render.hpp"

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

bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }
bool near(const Vec3f& a, const Vec3f& b, float eps = 1e-3f) {
    return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps);
}

// ---------------------------------------------------------------- geometry

void test_geometry() {
    Vec3f a{1, 2, 3}, b{4, 5, 6};
    check(near(a + b, Vec3f{5, 7, 9}), "vector add");
    check(near(a - b, Vec3f{-3, -3, -3}), "vector sub");
    check(near(a * 2.f, Vec3f{2, 4, 6}), "scalar mul");
    check(near(a.dot(b), 32.f), "dot product");
    check(near(Vec3f{1, 0, 0}.cross(Vec3f{0, 1, 0}), Vec3f{0, 0, 1}),
          "cross product, right-handed");
    check(near(Vec3f{3, 4, 0}.norm(), 5.f), "norm, 3-4-5 triangle");
    check(near(Vec3f{3, 4, 0}.normalized().norm(), 1.f), "normalized has unit length");
    check(near(Vec3f{0, 0, 0}.normalized(), Vec3f{0, 0, 0}),
          "normalizing the zero vector doesn't divide by zero");
}

// -------------------------------------------------------------------- model

void test_obj_loading() {
    // A single triangle, plus a stray "vt"/"vn"/comment/blank line this
    // position-only loader must simply ignore.
    std::istringstream in(
        "# a lone triangle\n"
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "vt 0 0\n"
        "\n"
        "f 1 2 3\n");
    Model m;
    check(m.load(in), "load() reports success on a well-formed file");
    check(m.num_verts() == 3, "three vertex lines parsed");
    check(m.num_faces() == 1, "one face line parsed");
    auto v = m.face_verts(0);
    check(near(v[0], Vec3f{0, 0, 0}) && near(v[1], Vec3f{1, 0, 0}) && near(v[2], Vec3f{0, 1, 0}),
          "face_verts returns the triangle's own three positions, in file order");

    std::istringstream empty("");
    Model e;
    check(!e.load(empty), "an empty stream is reported as a load failure, not a crash");
}

void test_obj_loading_ignores_texture_and_normal_indices() {
    // "f a/vt/vn ..." must resolve to the same vertex index as bare "f a ...".
    std::istringstream in(
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1/1/1 2/2/1 3/3/1\n");
    Model m;
    m.load(in);
    check(m.num_faces() == 1, "slash-qualified face indices still parse");
    auto v = m.face_verts(0);
    check(near(v[1], Vec3f{1, 0, 0}), "the position index (before the first slash) is the one used");
}

void test_face_normal() {
    // A right triangle in the z=0 plane, wound counter-clockwise as seen
    // from +z, should have an outward normal of exactly (0, 0, 1).
    std::istringstream in(
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n");
    Model m;
    m.load(in);
    check(near(m.face_normal(0), Vec3f{0, 0, 1}), "CCW triangle in the xy-plane normals to +z");

    // Reversing the winding flips the normal -- this is exactly what
    // back-face culling depends on to tell a front face from a back one.
    std::istringstream reversed(
        "v 0 0 0\n"
        "v 0 1 0\n"
        "v 1 0 0\n"
        "f 1 2 3\n");
    Model r;
    r.load(reversed);
    check(near(r.face_normal(0), Vec3f{0, 0, -1}), "reversing the winding reverses the normal");

    // Three collinear points span no area and have no well-defined normal
    // direction -- this must come back as the zero vector, not NaN.
    std::istringstream degenerate(
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 2 0 0\n"
        "f 1 2 3\n");
    Model d;
    d.load(degenerate);
    Vec3f n = d.face_normal(0);
    check(near(n, Vec3f{0, 0, 0}), "a degenerate (zero-area) face normals to the zero vector");
}

// ---------------------------------------------------------------------- line

// Every pixel a line touches, as an unordered count -- enough to check
// "how many pixels" and "no gaps" without hardcoding Bresenham's exact
// step-by-step choices.
int count_lit_pixels(const Image& img) {
    int n = 0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            Color c = img.get(x, y);
            if (c.r || c.g || c.b) n++;
        }
    }
    return n;
}

void test_line_drawing() {
    Color white{255, 255, 255};

    {
        Image img(20, 20);
        line({2, 5}, {2, 15}, img, white);
        check(count_lit_pixels(img) == 11, "vertical line lights exactly dy+1 pixels");
    }
    {
        Image img(20, 20);
        line({2, 5}, {12, 5}, img, white);
        check(count_lit_pixels(img) == 11, "horizontal line lights exactly dx+1 pixels");
    }
    {
        // A steep line (rises 10 for 3 across) is the case a naive
        // x-stepping loop gets wrong: without transposing to walk the
        // long axis, it draws only 3-4 pixels and leaves vertical gaps.
        Image img(20, 20);
        line({2, 2}, {5, 12}, img, white);
        check(count_lit_pixels(img) == 11, "steep line still lights one pixel per row, no gaps");
    }
    {
        // Drawing a line and its reverse must produce the identical pixel
        // set -- the left-to-right normalization is what guarantees this.
        Image a(20, 20), b(20, 20);
        line({3, 2}, {14, 9}, a, white);
        line({14, 9}, {3, 2}, b, white);
        for (int y = 0; y < 20; ++y) {
            for (int x = 0; x < 20; ++x) {
                Color pa = a.get(x, y), pb = b.get(x, y);
                if (pa.r != pb.r) {
                    check(false, "line(p0,p1) and line(p1,p0) must match");
                    goto done_symmetry;
                }
            }
        }
        check(true, "line(p0,p1) and line(p1,p0) draw the same pixels");
    done_symmetry:;
    }
}

// ----------------------------------------------------------------- triangle

void test_barycentric() {
    Vec3f a{0, 0, 0}, b{10, 0, 0}, c{0, 10, 0};
    check(near(barycentric(a, b, c, Vec3f{0, 0, 0}), Vec3f{1, 0, 0}), "vertex a has weight (1,0,0)");
    check(near(barycentric(a, b, c, Vec3f{10, 0, 0}), Vec3f{0, 1, 0}), "vertex b has weight (0,1,0)");
    check(near(barycentric(a, b, c, Vec3f{0, 10, 0}), Vec3f{0, 0, 1}), "vertex c has weight (0,0,1)");

    Vec3f center = barycentric(a, b, c, Vec3f{10.f / 3, 10.f / 3, 0});
    check(near(center, Vec3f{1.f / 3, 1.f / 3, 1.f / 3}), "centroid has equal weights");

    Vec3f outside = barycentric(a, b, c, Vec3f{20, 20, 0});
    check(outside.x < 0 || outside.y < 0 || outside.z < 0,
          "a point outside the triangle has at least one negative weight");

    // Three collinear points: no area, so no valid weights exist.
    Vec3f degenerate = barycentric(Vec3f{0, 0, 0}, Vec3f{1, 0, 0}, Vec3f{2, 0, 0}, Vec3f{1, 1, 0});
    check(degenerate.x < 0 && degenerate.y < 0 && degenerate.z < 0,
          "a degenerate triangle reports every point as outside, not a division by zero");
}

void test_triangle_fill() {
    Image img(20, 20);
    Color red{255, 0, 0};
    triangle({2, 2, 0}, {17, 2, 0}, {2, 17, 0}, img, red, nullptr);

    check(img.get(3, 3).r == 255, "a point well inside the triangle is filled");
    check(img.get(18, 18).r == 0, "a point well outside the triangle is left untouched");
    check(img.get(0, 0).r == 0, "the far corner outside the bounding box is untouched");
}

void test_zbuffer_picks_nearer_surface() {
    // Two overlapping triangles at the same (x, y) footprint but
    // different depths -- the setup a z-buffer exists to resolve.
    Vec3f near_tri[3] = {{2, 2, -1}, {17, 2, -1}, {2, 17, -1}};  // z = -1, closer
    Vec3f far_tri[3] = {{2, 2, 1}, {17, 2, 1}, {2, 17, 1}};      // z = +1, farther
    Color red{255, 0, 0}, blue{0, 0, 255};

    {
        // No z-buffer: whichever is drawn LAST wins, regardless of depth.
        // Drawing the near triangle first, then the far one, lets the
        // farther surface incorrectly paint over the nearer one -- this
        // is the exact bug lesson 3 exists to fix.
        Image img(20, 20);
        triangle(near_tri[0], near_tri[1], near_tri[2], img, red, nullptr);
        triangle(far_tri[0], far_tri[1], far_tri[2], img, blue, nullptr);
        check(img.get(5, 5).b == 255,
              "without a z-buffer, drawing farther-after-nearer wrongly shows the far color");
    }
    {
        // With a z-buffer, the same draw order now produces the correct
        // result: whichever surface has the smaller z always wins, no
        // matter which was drawn first.
        Image img(20, 20);
        ZBuffer zbuf(20, 20);
        triangle(near_tri[0], near_tri[1], near_tri[2], img, red, &zbuf);
        triangle(far_tri[0], far_tri[1], far_tri[2], img, blue, &zbuf);
        check(img.get(5, 5).r == 255,
              "with a z-buffer, the nearer (smaller z) surface wins regardless of draw order");
    }
    {
        // Reversing the draw order must not change the outcome -- that's
        // the whole point of depth-testing instead of relying on order.
        Image img(20, 20);
        ZBuffer zbuf(20, 20);
        triangle(far_tri[0], far_tri[1], far_tri[2], img, blue, &zbuf);
        triangle(near_tri[0], near_tri[1], near_tri[2], img, red, &zbuf);
        check(img.get(5, 5).r == 255, "z-buffered result is independent of draw order");
    }
}

}  // namespace

int main() {
    test_geometry();
    test_obj_loading();
    test_obj_loading_ignores_texture_and_normal_indices();
    test_face_normal();
    test_line_drawing();
    test_barycentric();
    test_triangle_fill();
    test_zbuffer_picks_nearer_surface();

    std::printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
