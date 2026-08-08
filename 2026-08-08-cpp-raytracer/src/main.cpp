#include <iostream>
#include <vector>

#include "render.hpp"

int main(int argc, char** argv) {
    Material ivory{Vec3f{0.4f, 0.4f, 0.3f}, Albedo{0.6f, 0.3f, 0.1f, 0.0f}, 50.f, 1.0f};
    Material glass{Vec3f{0.6f, 0.7f, 0.8f}, Albedo{0.0f, 0.5f, 0.1f, 0.8f}, 125.f, 1.5f};
    Material mirror{Vec3f{1.0f, 1.0f, 1.0f}, Albedo{0.0f, 10.0f, 0.8f, 0.0f}, 1425.f, 1.0f};
    Material rubber{Vec3f{0.3f, 0.1f, 0.1f}, Albedo{0.9f, 0.1f, 0.0f, 0.0f}, 10.f, 1.0f};

    std::vector<Sphere> spheres{
        {Vec3f{-3, 0, -16}, 2, ivory},
        {Vec3f{-1.0f, -1.5f, -12}, 2, glass},
        {Vec3f{1.5f, -0.5f, -18}, 3, rubber},
        {Vec3f{7, 5, -18}, 4, mirror},
    };

    std::vector<Light> lights{
        {Vec3f{-20, 20, 20}, 1.5f},
        {Vec3f{30, 50, -25}, 1.8f},
        {Vec3f{30, 20, 30}, 1.7f},
    };

    int width = argc > 1 ? std::stoi(argv[1]) : 1024;
    int height = argc > 2 ? std::stoi(argv[2]) : 768;
    std::string out = argc > 3 ? argv[3] : "renders/out.ppm";

    render(out, width, height, 3.14159265f / 3.f, spheres, lights);
    std::cout << "wrote " << out << " (" << width << "x" << height << ")\n";
    return 0;
}
