// Minimal wall raycaster (after ssloy/tinyraycaster), headless: renders to a PPM.
#pragma once
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

struct Map {
    int w, h;
    std::string cells;  // ' ' empty, digit = wall texture id
    bool wall(int x, int y) const {
        return x < 0 || y < 0 || x >= w || y >= h || cells[x + y * w] != ' ';
    }
    int id(int x, int y) const { return cells[x + y * w] - '0'; }
};

struct Player { float x, y, a, fov; };

struct Hit { float dist; int id; bool hit; };

// Marches a ray in small steps until it enters a wall cell or exceeds max_dist.
inline Hit cast(const Map& m, const Player& p, float angle, float max_dist = 20.f) {
    for (float t = 0; t < max_dist; t += 0.01f) {
        float cx = p.x + t * std::cos(angle), cy = p.y + t * std::sin(angle);
        if (m.wall((int)std::floor(cx), (int)std::floor(cy))) {
            int ix = (int)std::floor(cx), iy = (int)std::floor(cy);
            return {t, (ix < 0 || iy < 0 || ix >= m.w || iy >= m.h) ? 0 : m.id(ix, iy), true};
        }
    }
    return {max_dist, 0, false};
}

// Height in pixels of a wall slice; fish-eye corrected with cos(angle - view).
inline int slice_height(int screen_h, float dist, float angle, float view) {
    float d = dist * std::cos(angle - view);
    if (d < 0.01f) d = 0.01f;
    return (int)(screen_h / d);
}

inline uint32_t pack(uint8_t r, uint8_t g, uint8_t b) { return r | (g << 8) | (b << 16); }

inline std::vector<uint32_t> render(const Map& m, const Player& p, int W, int H) {
    static const uint32_t palette[] = {pack(120,120,120), pack(200,60,60), pack(60,160,60), pack(60,80,200)};
    std::vector<uint32_t> fb(W * H, pack(30, 30, 30));
    for (int y = H / 2; y < H; y++) for (int x = 0; x < W; x++) fb[x + y * W] = pack(70, 55, 40);  // floor
    for (int i = 0; i < W; i++) {
        float angle = p.a - p.fov / 2 + p.fov * i / W;
        Hit h = cast(m, p, angle);
        if (!h.hit) continue;
        int sh = slice_height(H, h.dist, angle, p.a);
        uint32_t c = palette[h.id % 4];
        float shade = 1.f / (1.f + 0.15f * h.dist);
        c = pack((c & 255) * shade, ((c >> 8) & 255) * shade, ((c >> 16) & 255) * shade);
        for (int y = std::max(0, (H - sh) / 2); y < std::min(H, (H + sh) / 2); y++) fb[i + y * W] = c;
    }
    return fb;
}

inline std::string to_ppm(const std::vector<uint32_t>& fb, int W, int H) {
    std::string s = "P6\n" + std::to_string(W) + " " + std::to_string(H) + "\n255\n";
    for (uint32_t c : fb) { s += (char)(c & 255); s += (char)((c >> 8) & 255); s += (char)((c >> 16) & 255); }
    return s;
}
