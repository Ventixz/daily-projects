#include "render.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>

Vec3f sky_color(const Vec3f& dir) {
    float t = 0.5f * (dir.normalized().y + 1.f); // remap [-1,1] -> [0,1]
    Vec3f horizon{0.82f, 0.86f, 0.92f};
    Vec3f zenith{0.15f, 0.35f, 0.75f};
    return horizon * (1 - t) + zenith * t;
}

bool scene_intersect(const Vec3f& orig, const Vec3f& dir,
                      const std::vector<Sphere>& spheres,
                      Vec3f& hit, Vec3f& N, Material& material) {
    float spheres_dist = std::numeric_limits<float>::max();
    for (const auto& s : spheres) {
        float dist_i;
        if (s.ray_intersect(orig, dir, dist_i) && dist_i < spheres_dist) {
            spheres_dist = dist_i;
            hit = orig + dir * dist_i;
            N = (hit - s.center).normalized();
            material = s.material;
        }
    }

    // Checkerboard floor: an infinite plane at y = -4, clipped to a
    // finite tile so the render doesn't spend rays on a floor that never
    // resolves to anything but a flat color at the horizon.
    float checkerboard_dist = std::numeric_limits<float>::max();
    if (std::fabs(dir.y) > 1e-3f) {
        float d = -(orig.y + 4) / dir.y;
        Vec3f pt = orig + dir * d;
        if (d > 0 && std::fabs(pt.x) < 10 && pt.z < -10 && pt.z > -30 && d < spheres_dist) {
            checkerboard_dist = d;
            hit = pt;
            N = Vec3f{0, 1, 0};
            bool light_tile = (int(std::floor(0.5f * hit.x + 1000)) + int(std::floor(0.5f * hit.z))) & 1;
            material.diffuse_color = light_tile ? Vec3f{0.3f, 0.3f, 0.3f} : Vec3f{0.85f, 0.75f, 0.6f};
            material.albedo = Albedo{1.f, 0.f, 0.f, 0.f};
        }
    }

    return std::min(spheres_dist, checkerboard_dist) < 1000;
}

Vec3f cast_ray(const Vec3f& orig, const Vec3f& dir,
               const std::vector<Sphere>& spheres,
               const std::vector<Light>& lights, int depth) {
    Vec3f point, N;
    Material material;
    if (depth > kMaxDepth || !scene_intersect(orig, dir, spheres, point, N, material)) {
        return sky_color(dir);
    }

    // Offset the recursion/shadow origin along the normal, away from the
    // surface, on whichever side the ray is exiting toward -- without
    // this a reflection/shadow ray immediately re-intersects the same
    // surface it just left, at t~0, due to floating point error.
    Vec3f reflect_dir = reflect(dir, N).normalized();
    Vec3f reflect_orig = reflect_dir.dot(N) < 0 ? point - N * 1e-3f : point + N * 1e-3f;
    Vec3f reflect_color = cast_ray(reflect_orig, reflect_dir, spheres, lights, depth + 1);

    Vec3f refract_color{0, 0, 0};
    if (material.albedo.refraction > 0) {
        bool tir = false;
        Vec3f refract_dir = refract(dir, N, material.refractive_index, 1.f, tir).normalized();
        if (!tir) {
            Vec3f refract_orig = refract_dir.dot(N) < 0 ? point - N * 1e-3f : point + N * 1e-3f;
            refract_color = cast_ray(refract_orig, refract_dir, spheres, lights, depth + 1);
        } else {
            refract_color = reflect_color; // total internal reflection: light that would have transmitted, reflects instead
        }
    }

    float diffuse_intensity = 0, specular_intensity = 0;
    for (const auto& light : lights) {
        Vec3f light_dir = (light.position - point).normalized();
        float light_dist = (light.position - point).norm();

        Vec3f shadow_orig = light_dir.dot(N) < 0 ? point - N * 1e-3f : point + N * 1e-3f;
        Vec3f shadow_pt, shadow_N;
        Material tmp_material;
        if (scene_intersect(shadow_orig, light_dir, spheres, shadow_pt, shadow_N, tmp_material) &&
            (shadow_pt - shadow_orig).norm() < light_dist) {
            continue; // something between the point and the light: fully shadowed for this light
        }

        diffuse_intensity += light.intensity * std::max(0.f, light_dir.dot(N));
        specular_intensity +=
            std::pow(std::max(0.f, reflect(-1.f * light_dir, N).dot(dir)), material.specular_exponent) *
            light.intensity;
    }

    return material.diffuse_color * diffuse_intensity * material.albedo.diffuse +
           Vec3f{1, 1, 1} * specular_intensity * material.albedo.specular +
           reflect_color * material.albedo.reflection + refract_color * material.albedo.refraction;
}

void render(const std::string& path, int width, int height, float fov_radians,
            const std::vector<Sphere>& spheres, const std::vector<Light>& lights) {
    std::vector<Vec3f> framebuffer(width * height);

    for (int j = 0; j < height; ++j) {
        for (int i = 0; i < width; ++i) {
            float x = (2 * (i + 0.5f) / width - 1) * std::tan(fov_radians / 2.f) * width / height;
            float y = -(2 * (j + 0.5f) / height - 1) * std::tan(fov_radians / 2.f);
            Vec3f dir = Vec3f{x, y, -1}.normalized();
            framebuffer[i + j * width] = cast_ray(Vec3f{0, 0, 0}, dir, spheres, lights);
        }
    }

    std::ofstream ofs(path, std::ios::binary);
    ofs << "P6\n" << width << " " << height << "\n255\n";
    for (auto& c : framebuffer) {
        // Rescale by the brightest channel instead of hard-clamping each
        // channel independently: a light that pushes red to 1.4 while
        // green/blue stay under 1 should dim toward white, not just lose
        // its red and shift color -- clamping each channel separately
        // does the latter and visibly discolors bright highlights.
        float max_c = std::max(c.x, std::max(c.y, c.z));
        if (max_c > 1) c = c * (1.f / max_c);
        for (int k = 0; k < 3; ++k) {
            unsigned char byte =
                static_cast<unsigned char>(255 * std::max(0.f, std::min(1.f, c[k])));
            ofs.write(reinterpret_cast<char*>(&byte), 1);
        }
    }
}
