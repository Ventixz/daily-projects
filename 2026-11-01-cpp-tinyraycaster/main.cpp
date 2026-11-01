#include <cstdio>
#include <cstdlib>
#include <fstream>
#include "raycaster.h"

Map default_map() {
    return {16, 16,
        "0000222222220000"
        "1              0"
        "1      11111   0"
        "1     0        0"
        "0     0  1110000"
        "0     3        0"
        "0   10000      0"
        "0   0   11100  0"
        "0   0   0      0"
        "0   0   1  00000"
        "0       1      0"
        "2       1      0"
        "0       0      0"
        "0 0000000      0"
        "0              0"
        "0002222222200000"};
}

int main(int argc, char** argv) {
    const char* out = argc > 1 ? argv[1] : "out.ppm";
    float a = argc > 2 ? (float)atof(argv[2]) : 1.523f;
    Player p{3.456f, 2.345f, a, (float)M_PI / 3};
    int W = 512, H = 384;
    std::ofstream(out, std::ios::binary) << to_ppm(render(default_map(), p, W, H), W, H);
    std::printf("wrote %s (%dx%d)\n", out, W, H);
}
