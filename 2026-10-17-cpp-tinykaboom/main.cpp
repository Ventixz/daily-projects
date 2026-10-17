#include <cstdio>
#include <cstdlib>
#include <fstream>

#include "kaboom.h"

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "kaboom.ppm";
    int width = argc > 2 ? std::atoi(argv[2]) : 640;
    int height = argc > 3 ? std::atoi(argv[3]) : 480;
    if (width <= 0 || height <= 0) {
        std::fprintf(stderr, "usage: %s [out.ppm] [width] [height]\n", argv[0]);
        return 1;
    }
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        std::fprintf(stderr, "cannot open %s\n", path);
        return 1;
    }
    f << to_ppm(width, height, render(width, height));
    std::printf("wrote %s (%dx%d)\n", path, width, height);
    return 0;
}
