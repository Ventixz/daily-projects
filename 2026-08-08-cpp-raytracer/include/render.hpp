#pragma once

#include <string>
#include <vector>
#include "geometry.hpp"
#include "scene.hpp"

constexpr int kMaxDepth = 4;

// Procedural sky: a vertical gradient from a warm horizon to a cool
// zenith, keyed off the ray direction's y component. Written instead of
// loading a background photo (the tutorial's envmap.jpg) so the whole
// project stays free of an image-decoding dependency -- see LEARNING.md.
Vec3f sky_color(const Vec3f& dir);

// Finds the nearest surface (sphere or checkerboard floor) a ray hits.
// On a hit, fills point/N/material and returns true.
bool scene_intersect(const Vec3f& orig, const Vec3f& dir,
                      const std::vector<Sphere>& spheres,
                      Vec3f& hit, Vec3f& N, Material& material);

// Traces one ray to a color: direct lighting (diffuse + Phong specular,
// with shadow rays) plus recursive mirror reflection and glass
// refraction, bottoming out at kMaxDepth or a ray that hits nothing.
Vec3f cast_ray(const Vec3f& orig, const Vec3f& dir,
               const std::vector<Sphere>& spheres,
               const std::vector<Light>& lights, int depth = 0);

// Renders the scene to a binary PPM (P6) file at the given path.
void render(const std::string& path, int width, int height, float fov_radians,
            const std::vector<Sphere>& spheres, const std::vector<Light>& lights);
