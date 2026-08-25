#pragma once

#include <string>
#include <vector>

#include "geometry.hpp"

// A row-major RGB framebuffer, addressed with (0, 0) at the BOTTOM-left --
// matching the math convention (y grows up) that the rest of the renderer
// uses for model coordinates. write_ppm() flips rows when it serializes,
// since the PPM/most image formats store row 0 as the TOP of the image.
// Getting that flip backwards is the classic "my render is upside down"
// bug this class exists to localize to one place.
class Image {
public:
    Image(int width, int height) : width_(width), height_(height), pixels_(width * height) {}

    int width() const { return width_; }
    int height() const { return height_; }

    void set(int x, int y, Color c) {
        if (x < 0 || x >= width_ || y < 0 || y >= height_) return;
        pixels_[y * width_ + x] = c;
    }

    Color get(int x, int y) const {
        if (x < 0 || x >= width_ || y < 0 || y >= height_) return Color{};
        return pixels_[y * width_ + x];
    }

    void write_ppm(const std::string& path) const;

private:
    int width_, height_;
    std::vector<Color> pixels_;
};
