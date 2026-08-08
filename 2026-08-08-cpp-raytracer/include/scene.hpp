#pragma once

#include <vector>
#include "geometry.hpp"

struct Material {
    Vec3f diffuse_color{0, 0, 0};
    Albedo albedo{};
    float specular_exponent = 1;
    float refractive_index = 1;
};

struct Light {
    Vec3f position;
    float intensity;
};

struct Sphere {
    Vec3f center;
    float radius;
    Material material;

    // Solves |orig + t*dir - center|^2 == radius^2 for the smallest
    // positive t. dir must be a unit vector. Returns false if the ray
    // misses, or hits only behind the origin.
    bool ray_intersect(const Vec3f& orig, const Vec3f& dir, float& t0) const {
        Vec3f L = center - orig;
        float tca = L.dot(dir);          // projection of L onto the ray
        float d2 = L.dot(L) - tca * tca; // squared distance, center to ray line
        float r2 = radius * radius;
        if (d2 > r2) return false;
        float thc = std::sqrt(r2 - d2);  // half-chord length
        t0 = tca - thc;
        float t1 = tca + thc;
        if (t0 < 0) t0 = t1;   // origin is inside the sphere: use the exit point
        if (t0 < 0) return false; // both intersections are behind the origin
        return true;
    }
};

// Mirror-reflect direction I about a surface normal N (both must be unit
// vectors). Standard identity: R = I - 2*(I.N)*N.
inline Vec3f reflect(const Vec3f& I, const Vec3f& N) {
    return I - N * 2.f * I.dot(N);
}

// Snell's law, handling the ray entering vs. exiting a medium (that's
// what the two branches on cosi are for) and total internal reflection.
// `tir` is set to true when the discriminant goes negative -- steep
// enough angles through glass genuinely have no transmitted ray, only a
// reflected one, and the caller (cast_ray) needs to know that rather
// than being handed a meaningless direction to trace.
inline Vec3f refract(const Vec3f& I, const Vec3f& N, float eta_t, float eta_i, bool& tir) {
    float cosi = -std::max(-1.f, std::min(1.f, I.dot(N)));
    if (cosi < 0) return refract(I, -N, eta_i, eta_t, tir); // exiting the medium: flip normal, swap indices
    float eta = eta_i / eta_t;
    float k = 1 - eta * eta * (1 - cosi * cosi);
    tir = k < 0;
    if (tir) return Vec3f{0, 0, 0};
    return I * eta + N * (eta * cosi - std::sqrt(k));
}
