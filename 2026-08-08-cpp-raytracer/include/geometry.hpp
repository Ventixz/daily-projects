#pragma once

#include <cmath>

// A minimal 3D vector, built only for what the ray tracer needs: no
// swizzling, no SIMD, no template-over-dimension cleverness.
struct Vec3f {
    float x = 0, y = 0, z = 0;

    Vec3f() = default;
    Vec3f(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    float& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }
    float operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }

    Vec3f operator+(const Vec3f& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3f operator-(const Vec3f& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3f operator-() const { return {-x, -y, -z}; }
    Vec3f operator*(float s) const { return {x * s, y * s, z * s}; }
    // Component-wise product, used to tint a light's contribution by a
    // material's diffuse color -- NOT the dot product.
    Vec3f operator*(const Vec3f& o) const { return {x * o.x, y * o.y, z * o.z}; }

    float dot(const Vec3f& o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3f cross(const Vec3f& o) const {
        return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
    }

    float norm() const { return std::sqrt(dot(*this)); }
    Vec3f normalized() const {
        float n = norm();
        return n > 0 ? (*this) * (1.f / n) : Vec3f{0, 0, 0};
    }
};

inline Vec3f operator*(float s, const Vec3f& v) { return v * s; }

// A 4-component "weight" vector: how much a surface point's final color
// comes from diffuse light, specular highlight, mirror reflection, and
// refraction, respectively. Kept separate from Vec3f (a color/point/
// direction) because mixing the two by accident is exactly the kind of
// bug a distinct type in the constructor's signature makes structurally
// impossible.
struct Albedo {
    float diffuse = 1, specular = 0, reflection = 0, refraction = 0;
};
