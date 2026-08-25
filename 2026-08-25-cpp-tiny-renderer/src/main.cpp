// Renders four PPMs into renders/ that show the pipeline building up in
// stages: wireframe -> filled + shaded -> filled without a z-buffer (the
// bug) -> filled with one (the fix). Run `make run` to produce them, or
// `tools/ppm_to_png.py` to view them as PNG.
#include <fstream>
#include <iostream>

#include "geometry.hpp"
#include "image.hpp"
#include "model.hpp"
#include "render.hpp"

namespace {

constexpr int WIDTH = 400;
constexpr int HEIGHT = 400;

// The camera looks down +z from z = -infinity, so a smaller z is closer
// to it. This project only ever needs that ordering, not a full camera
// matrix -- there's no perspective divide (that's lesson 4 in the
// tutorial this project follows; see LEARNING.md for the scope cut).
const Vec3f kViewDir{0, 0, 1};

// A headlamp: the light sits at the camera and points the same way it's
// looking, so every face this project's back-face cull lets through
// necessarily faces at least partly toward it too.
const Vec3f kLightDir{0, 0, -1};

// Model space is roughly [-1, 1] on x and y; map that square onto the
// pixel grid. z is left untouched -- it's only ever used for depth
// comparisons, never converted to a pixel coordinate.
Vec3f to_screen(const Vec3f& v) {
    return {(v.x + 1.f) * WIDTH / 2.f, (v.y + 1.f) * HEIGHT / 2.f, v.z};
}

Vec2i to_pixel(const Vec3f& screen) {
    return {static_cast<int>(screen.x), static_cast<int>(screen.y)};
}

bool load_or_die(Model& m, const std::string& path) {
    std::ifstream in(path);
    if (!in || !m.load(in)) {
        std::cerr << "failed to load " << path << "\n";
        return false;
    }
    return true;
}

void render_wireframe(const Model& cube, const std::string& out_path) {
    Image img(WIDTH, HEIGHT);
    Color white{255, 255, 255};
    for (int f = 0; f < cube.num_faces(); ++f) {
        auto v = cube.face_verts(f);
        Vec2i a = to_pixel(to_screen(v[0]));
        Vec2i b = to_pixel(to_screen(v[1]));
        Vec2i c = to_pixel(to_screen(v[2]));
        line(a, b, img, white);
        line(b, c, img, white);
        line(c, a, img, white);
    }
    img.write_ppm(out_path);
}

void render_shaded(const Model& cube, const std::string& out_path) {
    Image img(WIDTH, HEIGHT);
    for (int f = 0; f < cube.num_faces(); ++f) {
        Vec3f n = cube.face_normal(f);
        float intensity = n.dot(kLightDir);
        bool front_facing = n.dot(kViewDir) < 0;
        if (!front_facing || intensity <= 0) continue;  // back-face cull

        auto v = cube.face_verts(f);
        unsigned char level = static_cast<unsigned char>(255 * intensity);
        Color shade{level, level, level};
        triangle(to_screen(v[0]), to_screen(v[1]), to_screen(v[2]), img, shade, nullptr);
    }
    img.write_ppm(out_path);
}

// Draws both cubes' front-facing triangles in a fixed order (near cube,
// then far cube). With zbuf == nullptr, later draws always win, so the
// farther cube incorrectly paints over the nearer one wherever their
// footprints overlap. With a real ZBuffer, draw order stops mattering.
void render_overlap(const Model& near_cube, Color near_color, const Model& far_cube,
                     Color far_color, ZBuffer* zbuf, const std::string& out_path) {
    Image img(WIDTH, HEIGHT);
    auto draw_cube = [&](const Model& cube, Color color) {
        for (int f = 0; f < cube.num_faces(); ++f) {
            Vec3f n = cube.face_normal(f);
            if (n.dot(kViewDir) >= 0) continue;  // back-face cull
            auto v = cube.face_verts(f);
            triangle(to_screen(v[0]), to_screen(v[1]), to_screen(v[2]), img, color, zbuf);
        }
    };
    draw_cube(near_cube, near_color);
    draw_cube(far_cube, far_color);
    img.write_ppm(out_path);
}

}  // namespace

int main() {
    Model cube_solo, cube_near, cube_far;
    if (!load_or_die(cube_solo, "models/cube_solo.obj") ||
        !load_or_die(cube_near, "models/cube_near.obj") ||
        !load_or_die(cube_far, "models/cube_far.obj")) {
        return 1;
    }

    render_wireframe(cube_solo, "renders/wireframe.ppm");
    render_shaded(cube_solo, "renders/shaded_cube.ppm");

    Color red{220, 60, 60};
    Color blue{60, 90, 220};
    render_overlap(cube_near, red, cube_far, blue, nullptr, "renders/overlap_no_zbuffer.ppm");
    ZBuffer zbuf(WIDTH, HEIGHT);
    render_overlap(cube_near, red, cube_far, blue, &zbuf, "renders/overlap_zbuffer.ppm");

    std::cout << "wrote renders/wireframe.ppm, shaded_cube.ppm, "
                 "overlap_no_zbuffer.ppm, overlap_zbuffer.ppm\n";
    return 0;
}
