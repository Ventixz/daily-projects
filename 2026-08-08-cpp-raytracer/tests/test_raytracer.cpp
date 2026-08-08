// Hand-rolled test harness -- no GoogleTest/Catch2 install, matching this
// repo's no-install-needed convention (see 2026-07-28-cpp-chip8-emulator).

#include <cmath>
#include <cstdio>
#include <string>

#include "render.hpp"

namespace {

int g_passed = 0;
int g_failed = 0;

void check(bool cond, const std::string& name) {
    if (cond) {
        g_passed++;
    } else {
        g_failed++;
        std::printf("FAIL: %s\n", name.c_str());
    }
}

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }
bool near(const Vec3f& a, const Vec3f& b, float eps = 1e-4f) {
    return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps);
}

// ---------------------------------------------------------------- geometry

void test_geometry() {
    Vec3f a{1, 2, 3}, b{4, 5, 6};
    check(near(a + b, Vec3f{5, 7, 9}), "vector add");
    check(near(a - b, Vec3f{-3, -3, -3}), "vector sub");
    check(near(a * 2.f, Vec3f{2, 4, 6}), "scalar mul");
    check(near(a.dot(b), 32.f), "dot product");
    check(near(Vec3f{1, 0, 0}.cross(Vec3f{0, 1, 0}), Vec3f{0, 0, 1}), "cross product, right-handed");
    check(near(Vec3f{3, 4, 0}.norm(), 5.f), "norm, 3-4-5 triangle");
    check(near(Vec3f{3, 4, 0}.normalized().norm(), 1.f), "normalized has unit length");
    check(near(Vec3f{0, 0, 0}.normalized(), Vec3f{0, 0, 0}), "normalizing the zero vector doesn't divide by zero");
}

// ------------------------------------------------------------- reflect/refract

void test_reflect_refract() {
    // A ray straight down hitting a horizontal surface bounces straight up.
    check(near(reflect(Vec3f{0, -1, 0}, Vec3f{0, 1, 0}), Vec3f{0, 1, 0}), "reflect off horizontal surface, straight down");

    // A ray hitting a surface at 45 degrees reflects to the other 45 degrees.
    Vec3f r = reflect(Vec3f{1, -1, 0}.normalized(), Vec3f{0, 1, 0});
    check(near(r, Vec3f{1, 1, 0}.normalized()), "reflect at 45 degrees mirrors the incoming angle");

    // A ray entering glass (n=1.5) head-on should pass straight through,
    // undeviated -- refraction only bends oblique rays.
    bool tir = false;
    Vec3f t = refract(Vec3f{0, -1, 0}, Vec3f{0, 1, 0}, 1.5f, 1.f, tir);
    check(!tir, "head-on ray into glass: no total internal reflection");
    check(near(t, Vec3f{0, -1, 0}), "head-on ray into glass passes through undeviated");

    // A ray hitting glass at a steep enough grazing angle from the inside
    // undergoes total internal reflection: refract() must say so rather
    // than silently returning a bogus direction.
    bool tir2 = false;
    // Inside the glass (eta_i=1.5, eta_t=1.0 for air), incidence angle
    // near-grazing the surface exceeds the critical angle (~41.8 deg for n=1.5).
    Vec3f steep = Vec3f{0.99f, -0.1411f, 0}.normalized();
    refract(steep, Vec3f{0, 1, 0}, 1.f, 1.5f, tir2);
    check(tir2, "steep-angle ray inside glass hits total internal reflection");
}

// ------------------------------------------------------------- intersection

void test_sphere_intersect() {
    Sphere s{Vec3f{0, 0, -10}, 2, Material{}};
    float t;
    check(s.ray_intersect(Vec3f{0, 0, 0}, Vec3f{0, 0, -1}, t), "ray through sphere center hits");
    check(near(t, 8.f), "hit distance is center distance minus radius (10 - 2)");

    check(!s.ray_intersect(Vec3f{0, 5, 0}, Vec3f{0, 0, -1}, t), "parallel ray that misses the sphere entirely");

    // Origin inside the sphere: the near root is behind the origin, so
    // ray_intersect must fall through to the far (exit) root instead of
    // reporting a miss.
    Sphere enclosing{Vec3f{0, 0, 0}, 5, Material{}};
    check(enclosing.ray_intersect(Vec3f{0, 0, 0}, Vec3f{0, 0, -1}, t), "ray from inside a sphere hits the exit point");
    check(near(t, 5.f), "exit point is exactly the radius away");

    // Sphere entirely behind the ray origin: both roots negative.
    Sphere behind{Vec3f{0, 0, 10}, 2, Material{}};
    check(!behind.ray_intersect(Vec3f{0, 0, 0}, Vec3f{0, 0, -1}, t), "sphere behind the ray origin does not register as a hit");
}

// ------------------------------------------------------------------ scene

void test_scene_intersect_picks_nearest() {
    Material red{Vec3f{1, 0, 0}, Albedo{}, 1, 1};
    Material blue{Vec3f{0, 0, 1}, Albedo{}, 1, 1};
    std::vector<Sphere> spheres{
        {Vec3f{0, 0, -20}, 2, blue}, // farther
        {Vec3f{0, 0, -10}, 2, red},  // nearer
    };
    Vec3f hit, N;
    Material m;
    check(scene_intersect(Vec3f{0, 0, 0}, Vec3f{0, 0, -1}, spheres, hit, N, m), "ray hits something");
    check(near(m.diffuse_color, Vec3f{1, 0, 0}), "scene_intersect reports the nearer sphere, not just any hit");
}

void test_shadow() {
    Material diffuse_only{Vec3f{1, 1, 1}, Albedo{1, 0, 0, 0}, 1, 1};
    // point (0,0,-5) is the front-facing surface of a sphere centered at
    // (0,0,-10), i.e. normal (0,0,1) pointing back toward the light.
    Vec3f point{0, 0, -5}, N{0, 0, 1};
    Light light{Vec3f{0, 0, 50}, 2.0f};

    // An occluder on the *far* side of the surface point from the light
    // (more negative z) must not shadow it.
    Sphere far_side{Vec3f{0, 0, -20}, 1, diffuse_only};
    Vec3f light_dir = (light.position - point).normalized();
    float light_dist = (light.position - point).norm();
    Vec3f shadow_orig = light_dir.dot(N) < 0 ? point - N * 1e-3f : point + N * 1e-3f;
    Vec3f sp, sn;
    Material sm;
    std::vector<Sphere> clear_scene{far_side};
    bool occluded_far = scene_intersect(shadow_orig, light_dir, clear_scene, sp, sn, sm) &&
                         (sp - shadow_orig).norm() < light_dist;
    check(!occluded_far, "a sphere behind the surface point, away from the light, does not cast a shadow onto it");

    // An occluder directly on the segment from point to light (z=-3, between
    // -5 and 50) must shadow it.
    Sphere blocker{Vec3f{0, 0, -3}, 1, diffuse_only};
    std::vector<Sphere> blocked_scene{blocker};
    bool occluded = scene_intersect(shadow_orig, light_dir, blocked_scene, sp, sn, sm) &&
                     (sp - shadow_orig).norm() < light_dist;
    check(occluded, "a sphere sitting directly between surface and light casts a shadow");
}

void test_background_has_no_geometry() {
    std::vector<Sphere> empty_spheres{};
    std::vector<Light> lights{{Vec3f{0, 20, 0}, 1.f}};
    Vec3f c = cast_ray(Vec3f{0, 0, 0}, Vec3f{0, 0, -1}, empty_spheres, lights);
    check(near(c, sky_color(Vec3f{0, 0, -1})), "a ray that hits nothing returns exactly the sky color");
}

void test_mirror_reflects_scene_color() {
    // A dead-center hit on a mirror sphere reflects a ray straight back the
    // way it came: reflect((0,0,-1), N=(0,0,1)) = (0,0,1). So a camera ray
    // straight down -z off a mirror at (0,0,-10) bounces back through the
    // camera's own position and on toward +z -- put a lit red sphere there
    // and the mirror's returned color should carry its tint.
    Material mirror{Vec3f{1, 1, 1}, Albedo{0.f, 10.f, 1.0f, 0.f}, 1425.f, 1.f};
    Material red{Vec3f{1, 0.05f, 0.05f}, Albedo{0.9f, 0.1f, 0.f, 0.f}, 50.f, 1.f};
    std::vector<Sphere> spheres{
        {Vec3f{0, 0, -10}, 5, mirror},
        {Vec3f{0, 0, 10}, 2, red},
    };
    // Light sits on the camera's side (negative z) so it lands on the
    // red sphere's face that points back toward the mirror -- a light
    // placed past the sphere would light its far side instead, the one
    // the reflected ray never sees.
    std::vector<Light> lights{{Vec3f{0, 20, -20}, 1.8f}};

    Vec3f baseline_color = cast_ray(Vec3f{0, 0, 0}, Vec3f{0, 0, -1}, std::vector<Sphere>{spheres[0]}, lights);
    Vec3f color = cast_ray(Vec3f{0, 0, 0}, Vec3f{0, 0, -1}, spheres, lights);
    check(color.x > baseline_color.x + 0.05f,
          "mirror sphere's reflection picks up the red sphere sitting behind the camera's reflected ray");
}

}  // namespace

int main() {
    test_geometry();
    test_reflect_refract();
    test_sphere_intersect();
    test_scene_intersect_picks_nearest();
    test_shadow();
    test_background_has_no_geometry();
    test_mirror_reflects_scene_color();

    std::printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
