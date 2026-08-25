#pragma once

#include <cmath>

// A minimal 3D vector: just what a software rasterizer needs (points,
// directions, normals, colors -- all the same shape, so one type covers
// them). No SIMD, no template-over-dimension cleverness.
struct Vec3f {
    float x = 0, y = 0, z = 0;

    Vec3f() = default;
    Vec3f(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    Vec3f operator+(const Vec3f& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3f operator-(const Vec3f& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3f operator*(float s) const { return {x * s, y * s, z * s}; }

    float dot(const Vec3f& o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3f cross(const Vec3f& o) const {
        return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
    }

    float norm() const { return std::sqrt(dot(*this)); }
    // The zero vector has no direction to normalize toward, so it stays
    // zero rather than dividing by zero -- a degenerate (zero-area)
    // triangle's "normal" should read as "no normal", not NaN.
    Vec3f normalized() const {
        float n = norm();
        return n > 0 ? (*this) * (1.f / n) : Vec3f{0, 0, 0};
    }
};

// Integer screen-space point (pixel coordinates).
struct Vec2i {
    int x = 0, y = 0;
};

// 8-bit RGB color.
struct Color {
    unsigned char r = 0, g = 0, b = 0;
};
