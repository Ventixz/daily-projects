#include "render.hpp"

#include <algorithm>
#include <cmath>

void line(Vec2i p0, Vec2i p1, Image& img, Color c) {
    int x0 = p0.x, y0 = p0.y, x1 = p1.x, y1 = p1.y;

    // If the line rises more than it runs, walking x one column at a time
    // would skip rows and leave gaps. Transpose so we always walk along
    // the longer axis, then swap back when writing each pixel.
    bool steep = false;
    if (std::abs(x0 - x1) < std::abs(y0 - y1)) {
        std::swap(x0, y0);
        std::swap(x1, y1);
        steep = true;
    }
    // Always walk left to right, so line(a, b) and line(b, a) draw the
    // identical pixel set regardless of which endpoint was given first.
    if (x0 > x1) {
        std::swap(x0, x1);
        std::swap(y0, y1);
    }

    int dx = x1 - x0;
    int dy = y1 - y0;
    int step_y = (dy > 0) ? 1 : -1;
    int derror2 = std::abs(dy) * 2;
    int error2 = 0;
    int y = y0;
    for (int x = x0; x <= x1; ++x) {
        if (steep) {
            img.set(y, x, c);
        } else {
            img.set(x, y, c);
        }
        error2 += derror2;
        if (error2 > dx) {
            y += step_y;
            error2 -= dx * 2;
        }
    }
}

Vec3f barycentric(const Vec3f& a, const Vec3f& b, const Vec3f& c, const Vec3f& p) {
    Vec3f v0{b.x - a.x, b.y - a.y, 0};
    Vec3f v1{c.x - a.x, c.y - a.y, 0};
    Vec3f v2{p.x - a.x, p.y - a.y, 0};

    float d00 = v0.dot(v0);
    float d01 = v0.dot(v1);
    float d11 = v1.dot(v1);
    float d20 = v2.dot(v0);
    float d21 = v2.dot(v1);

    float denom = d00 * d11 - d01 * d01;
    if (std::abs(denom) < 1e-8f) return {-1, -1, -1};  // degenerate (zero-area) triangle

    float v = (d11 * d20 - d01 * d21) / denom;
    float w = (d00 * d21 - d01 * d20) / denom;
    float u = 1.f - v - w;
    return {u, v, w};
}

void triangle(const Vec3f& v0, const Vec3f& v1, const Vec3f& v2, Image& img, Color c,
              ZBuffer* zbuf) {
    float min_x = std::min({v0.x, v1.x, v2.x});
    float max_x = std::max({v0.x, v1.x, v2.x});
    float min_y = std::min({v0.y, v1.y, v2.y});
    float max_y = std::max({v0.y, v1.y, v2.y});

    int x_start = std::max(0, static_cast<int>(std::floor(min_x)));
    int x_end = std::min(img.width() - 1, static_cast<int>(std::ceil(max_x)));
    int y_start = std::max(0, static_cast<int>(std::floor(min_y)));
    int y_end = std::min(img.height() - 1, static_cast<int>(std::ceil(max_y)));

    for (int y = y_start; y <= y_end; ++y) {
        for (int x = x_start; x <= x_end; ++x) {
            Vec3f bc = barycentric(v0, v1, v2, Vec3f{x + 0.5f, y + 0.5f, 0});
            if (bc.x < 0 || bc.y < 0 || bc.z < 0) continue;

            float z = bc.x * v0.z + bc.y * v1.z + bc.z * v2.z;
            if (zbuf != nullptr) {
                if (!zbuf->test_and_set(x, y, z)) continue;
            }
            img.set(x, y, c);
        }
    }
}
