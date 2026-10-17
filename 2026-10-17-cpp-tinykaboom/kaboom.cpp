#include "kaboom.h"

#include <algorithm>
#include <limits>

float hash(float n) {
    float x = std::sin(n) * 43758.5453f;
    return x - std::floor(x);
}

float noise(const Vec3 &x) {
    Vec3 p(std::floor(x.x), std::floor(x.y), std::floor(x.z));
    Vec3 f = x - p;
    // Smoothstep so the gradient is continuous across lattice cells.
    f = Vec3(f.x * f.x * (3 - 2 * f.x), f.y * f.y * (3 - 2 * f.y), f.z * f.z * (3 - 2 * f.z));
    float n = p.dot(Vec3(1, 57, 113));
    return lerp(lerp(lerp(hash(n + 0), hash(n + 1), f.x),
                     lerp(hash(n + 57), hash(n + 58), f.x), f.y),
                lerp(lerp(hash(n + 113), hash(n + 114), f.x),
                     lerp(hash(n + 170), hash(n + 171), f.x), f.y),
                f.z);
}

static Vec3 rotate(const Vec3 &v) {
    return Vec3(Vec3(0.00f, 0.80f, 0.60f).dot(v),
                Vec3(-0.80f, 0.36f, -0.48f).dot(v),
                Vec3(-0.60f, -0.48f, 0.64f).dot(v));
}

float fractal_brownian_motion(const Vec3 &x) {
    Vec3 p = rotate(x);
    float f = 0;
    f += 0.5000f * noise(p); p = p * 2.32f;
    f += 0.2500f * noise(p); p = p * 3.03f;
    f += 0.1250f * noise(p); p = p * 2.61f;
    f += 0.0625f * noise(p);
    return f / 0.9375f;
}

float signed_distance(const Vec3 &p, float &displacement) {
    displacement = -fractal_brownian_motion(p * 3.4f) * kNoiseAmplitude;
    return p.norm() - (kSphereRadius + displacement);
}

bool sphere_trace(const Vec3 &orig, const Vec3 &dir, Vec3 &pos) {
    // Ray-sphere pre-check against the bounding sphere skips most background pixels.
    Vec3 to_center = Vec3() - orig;
    float tca = to_center.dot(dir);
    float d2 = to_center.dot(to_center) - tca * tca;
    float bound = kSphereRadius + kNoiseAmplitude;
    if (d2 > bound * bound) return false;

    pos = orig;
    for (int i = 0; i < 128; i++) {
        float disp;
        float d = signed_distance(pos, disp);
        if (d < 0) return true;
        // 0.1 factor: the displaced field is not a true distance, so step conservatively.
        pos = pos + dir * std::max(d * 0.1f, 0.01f);
        if (pos.norm() > bound + 1e-3f && pos.dot(dir) > 0) return false;
    }
    return false;
}

Vec3 palette_fire(float d) {
    const Vec3 yellow(1.7f, 1.3f, 1.0f);
    const Vec3 orange(1.0f, 0.6f, 0.0f);
    const Vec3 red(1.0f, 0.0f, 0.0f);
    const Vec3 darkgray(0.2f, 0.2f, 0.2f);
    const Vec3 gray(0.4f, 0.4f, 0.4f);

    float x = std::clamp(d, 0.f, 1.f);
    if (x < .25f) return lerp(gray, darkgray, x * 4.f);
    if (x < .50f) return lerp(darkgray, red, x * 4.f - 1.f);
    if (x < .75f) return lerp(red, orange, x * 4.f - 2.f);
    return lerp(orange, yellow, x * 4.f - 3.f);
}

Vec3 distance_field_normal(const Vec3 &pos) {
    const float eps = 0.1f;
    float d, nx, ny, nz;
    signed_distance(pos, d);
    nx = signed_distance(pos + Vec3(eps, 0, 0), d) - signed_distance(pos - Vec3(eps, 0, 0), d);
    ny = signed_distance(pos + Vec3(0, eps, 0), d) - signed_distance(pos - Vec3(0, eps, 0), d);
    nz = signed_distance(pos + Vec3(0, 0, eps), d) - signed_distance(pos - Vec3(0, 0, eps), d);
    return Vec3(nx, ny, nz).normalized();
}

std::vector<uint8_t> render(int width, int height) {
    const float fov = float(M_PI) / 3.f;
    const Vec3 camera(0, 0, 3);
    const Vec3 light(10, 10, 10);
    std::vector<uint8_t> rgb(size_t(width) * height * 3);

    for (int j = 0; j < height; j++) {
        for (int i = 0; i < width; i++) {
            float dir_x = (i + 0.5f) - width / 2.f;
            float dir_y = -(j + 0.5f) + height / 2.f;
            float dir_z = -height / (2.f * std::tan(fov / 2.f));
            Vec3 dir = Vec3(dir_x, dir_y, dir_z).normalized();

            Vec3 color(0.2f, 0.7f, 0.8f);  // background teal
            Vec3 hit;
            if (sphere_trace(camera, dir, hit)) {
                float disp;
                signed_distance(hit, disp);
                // Deeper valleys (larger |disp|) are hotter; remap into the palette's range.
                float heat = std::clamp((-disp - 0.2f) / 0.8f, 0.f, 1.f);
                float intensity = std::clamp((light - hit).normalized().dot(distance_field_normal(hit)), 0.4f, 1.f);
                color = palette_fire(heat) * intensity;
            }
            for (int c = 0; c < 3; c++) {
                float v = c == 0 ? color.x : c == 1 ? color.y : color.z;
                rgb[(size_t(j) * width + i) * 3 + c] = uint8_t(255 * std::clamp(v, 0.f, 1.f));
            }
        }
    }
    return rgb;
}

std::string to_ppm(int width, int height, const std::vector<uint8_t> &rgb) {
    std::string out = "P6\n" + std::to_string(width) + " " + std::to_string(height) + "\n255\n";
    out.append(reinterpret_cast<const char *>(rgb.data()), rgb.size());
    return out;
}
