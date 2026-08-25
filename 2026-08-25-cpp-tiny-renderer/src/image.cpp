#include "image.hpp"

#include <cstdio>

void Image::write_ppm(const std::string& path) const {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", width_, height_);
    // Row (height_ - 1) is our bottom-left origin's first row, which is
    // the TOP of the output image -- so rows are written back to front.
    for (int y = height_ - 1; y >= 0; --y) {
        for (int x = 0; x < width_; ++x) {
            Color c = get(x, y);
            std::fputc(c.r, f);
            std::fputc(c.g, f);
            std::fputc(c.b, f);
        }
    }
    std::fclose(f);
}
