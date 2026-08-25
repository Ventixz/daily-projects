#pragma once

#include <vector>

#include "geometry.hpp"
#include "image.hpp"

// Bresenham's line algorithm, integer-only. Handles the "steep" case (line
// taller than it is wide) by transposing x/y for the walk and swapping
// back on write -- without it, a steep line's naive x-stepping loop draws
// one pixel per column and leaves vertical gaps wherever the line rises
// more than one row per column.
void line(Vec2i p0, Vec2i p1, Image& img, Color c);

// A z-buffer: one depth value per pixel, addressed the same way as Image
// (row-major, (0,0) bottom-left). Starts at +infinity everywhere, meaning
// "nothing drawn here yet" -- so the very first triangle to touch a pixel
// always passes the depth test, however its z is signed.
class ZBuffer {
public:
    ZBuffer(int width, int height) : width_(width), height_(height), depth_(width * height, INF) {}

    // Returns true (and updates the stored depth) only if z is nearer
    // than whatever was last written to this pixel. "Nearer" means
    // smaller z, matching this project's camera convention: the camera
    // looks down +z, so smaller z is closer to it.
    bool test_and_set(int x, int y, float z) {
        if (x < 0 || x >= width_ || y < 0 || y >= height_) return false;
        float& cell = depth_[y * width_ + x];
        if (z < cell) {
            cell = z;
            return true;
        }
        return false;
    }

    static constexpr float INF = 1e9f;

private:
    int width_, height_;
    std::vector<float> depth_;
};

// Fills the triangle (v0, v1, v2) -- screen-space x/y, model-space z kept
// for depth-testing -- with a flat color, using a bounding-box scan and
// barycentric-coordinate membership test. If zbuf is non-null, each pixel
// is depth-tested (and the buffer updated) before it's written; pass
// nullptr to draw unconditionally in file order, which is how the "wrong
// object wins" bug gets demonstrated on purpose.
void triangle(const Vec3f& v0, const Vec3f& v1, const Vec3f& v2, Image& img, Color c,
              ZBuffer* zbuf);

// Barycentric coordinates of screen point p with respect to triangle
// (a, b, c), ignoring z. Returns {u, v, w} such that p == u*a + v*b + w*c
// and u+v+w == 1; any negative component means p is outside the triangle.
// Degenerate (zero-area, e.g. collinear) triangles return {-1,-1,-1} --
// guaranteed outside, rather than dividing by a zero denominator.
Vec3f barycentric(const Vec3f& a, const Vec3f& b, const Vec3f& c, const Vec3f& p);
