// Tiny Kaboom: a procedural explosion rendered by sphere tracing a noise-displaced sphere.
#pragma once
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    Vec3 operator+(const Vec3 &o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3 &o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    float dot(const Vec3 &o) const { return x * o.x + y * o.y + z * o.z; }
    float norm() const { return std::sqrt(dot(*this)); }
    Vec3 normalized() const { return *this * (1.f / norm()); }
};

inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
inline Vec3 lerp(const Vec3 &a, const Vec3 &b, float t) { return a + (b - a) * t; }

// Deterministic hash of a lattice point to [0,1).
float hash(float n);
// Trilinearly interpolated value noise in [0,1].
float noise(const Vec3 &x);
// Fractal Brownian motion: 4 octaves of rotated, doubled-frequency noise. Range is [0,1].
float fractal_brownian_motion(const Vec3 &x);

constexpr float kSphereRadius = 1.5f;
constexpr float kNoiseAmplitude = 1.0f;

// Signed distance to the displaced sphere; `displacement` receives the noise value used.
float signed_distance(const Vec3 &p, float &displacement);
// Marches from `orig` along unit `dir`; on hit returns true and sets `pos`.
bool sphere_trace(const Vec3 &orig, const Vec3 &dir, Vec3 &pos);
// Maps a [0,1] heat value to a fire palette (white-yellow -> orange -> red -> dark grey).
Vec3 palette_fire(float d);
Vec3 distance_field_normal(const Vec3 &pos);

// Renders a width x height frame as RGB bytes (3 per pixel, row-major, top row first).
std::vector<uint8_t> render(int width, int height);
std::string to_ppm(int width, int height, const std::vector<uint8_t> &rgb);
