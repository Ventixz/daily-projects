#include <cstdio>
#include <cstdlib>

#include "kaboom.h"

static int failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);  \
            failures++;                                                  \
        }                                                                \
    } while (0)

static bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }

int main() {
    // hash: deterministic and in [0,1)
    CHECK(hash(3.f) == hash(3.f));
    for (int i = -50; i < 50; i++) CHECK(hash(float(i)) >= 0.f && hash(float(i)) < 1.f);

    // noise: matches lattice hash at integer points, bounded, continuous
    CHECK(near(noise(Vec3(0, 0, 0)), hash(0.f)));
    CHECK(near(noise(Vec3(1, 2, 3)), hash(1 + 2 * 57 + 3 * 113.f)));
    for (int i = 0; i < 200; i++) {
        Vec3 p(i * 0.37f - 20, i * 0.11f, i * 0.53f - 5);
        float n = noise(p);
        CHECK(n >= 0.f && n <= 1.f);
        CHECK(std::fabs(noise(p + Vec3(1e-3f, 0, 0)) - n) < 0.02f);
    }

    // fbm: normalised into [0,1]
    for (int i = 0; i < 200; i++) {
        float f = fractal_brownian_motion(Vec3(i * 0.29f, i * 0.41f - 9, i * 0.07f));
        CHECK(f >= 0.f && f <= 1.f + 1e-5f);
    }

    // signed distance: origin is inside, far point outside
    float disp;
    CHECK(signed_distance(Vec3(0, 0, 0), disp) < 0);
    CHECK(signed_distance(Vec3(10, 0, 0), disp) > 0);

    // sphere tracing: hits through the centre, misses far off-axis, hit point near surface
    Vec3 hit;
    CHECK(sphere_trace(Vec3(0, 0, 3), Vec3(0, 0, -1), hit));
    CHECK(std::fabs(signed_distance(hit, disp)) < 0.1f);
    CHECK(!sphere_trace(Vec3(0, 0, 3), Vec3(1, 0, 0), hit));
    CHECK(!sphere_trace(Vec3(0, 10, 3), Vec3(0, 0, -1), hit));

    // palette: ends and clamping
    CHECK(near(palette_fire(1.f).x, 1.7f));
    CHECK(near(palette_fire(0.f).x, 0.4f));
    CHECK(near(palette_fire(-5.f).x, palette_fire(0.f).x));
    CHECK(near(palette_fire(9.f).x, palette_fire(1.f).x));
    CHECK(near(palette_fire(0.5f).x, 1.f) && near(palette_fire(0.5f).y, 0.f));

    // normal: unit length and points outward at the front hit
    Vec3 n = distance_field_normal(hit);
    CHECK(near(n.norm(), 1.f));
    CHECK(n.z > 0);

    // render: size, background corner, non-background centre; determinism; PPM header
    auto img = render(32, 24);
    CHECK(img.size() == 32u * 24u * 3u);
    CHECK(img[0] == 51 && img[1] == 178 && img[2] == 204);  // teal (0.2,0.7,0.8)
    size_t c = (12 * 32 + 16) * 3;
    CHECK(!(img[c] == 51 && img[c + 1] == 178 && img[c + 2] == 204));
    CHECK(img == render(32, 24));
    std::string ppm = to_ppm(32, 24, img);
    CHECK(ppm.rfind("P6\n32 24\n255\n", 0) == 0);
    CHECK(ppm.size() == std::string("P6\n32 24\n255\n").size() + img.size());

    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("all checks passed\n");
    return 0;
}
