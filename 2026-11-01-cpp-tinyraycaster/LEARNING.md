# Learning: 486 lines of C++: old-school FPS in a weekend (raycaster)

**Source:** "Tiny 3D graphics projects → tinyraycaster" in the C/C++ section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
Scope: the wall-rendering core only, headless (no SDL window, no textures/sprites). It writes a PPM image.

Build/run: `g++ -std=c++17 -O2 main.cpp -o rc && ./rc out.ppm 1.5`. Test: `g++ -std=c++17 test.cpp -o t && ./t`.

## Layout

- `raycaster.h` — `Map`, `Player`, `cast` (ray march), `slice_height`, `render`, `to_ppm`.
- `main.cpp` — renders a 16x16 map at 512x384 to a PPM.
- `test.cpp` — asserts on distances, bounds, max-distance, slice maths, framebuffer colours, PPM format, distance shading.

## What I learned

- **A "3D" view is one ray per screen column.** Each ray's hit distance sets the height of a vertical wall slice.
- **Fish-eye correction:** use the perpendicular distance `d * cos(ray_angle - view_angle)`, otherwise walls bulge.
- **Fixed-step marching is simple but approximate** (0.01 step, so ±0.01 error). DDA grid traversal is exact and faster; that's the natural next step.
- **Treat out-of-bounds as wall** so rays always terminate and no bounds checks leak into the renderer.
- **Headless output makes graphics testable:** assert on pixels and PPM bytes instead of eyeballing a window.
- Mistake caught by tests: I first assumed off-axis slices get shorter after correction; they get taller (perpendicular distance < Euclidean distance).

## Known limits

- No textures, sprites, or input loop; step marching is O(max_dist/step) per column.
