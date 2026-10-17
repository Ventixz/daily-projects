# Learning: Tiny Kaboom in C++

**Source:** "KABOOM! in 180 lines of bare C++" (tinykaboom) in the C/C++ "Tiny 3D graphics projects" section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
Plain C++17, no dependencies; renders a procedural explosion to a PPM image.

Run: `make && ./kaboom out.ppm 640 480`. Test: `make test`. `sample.png` is a 320x240 render.

## Layout

- `kaboom.h` / `kaboom.cpp` — `Vec3`, hash + value `noise`, `fractal_brownian_motion`, `signed_distance`,
  `sphere_trace`, `palette_fire`, `distance_field_normal`, `render`, `to_ppm`.
- `main.cpp` — CLI: output path, width, height.
- `test_kaboom.cpp` — assertion-style checks (hash/noise bounds and continuity, fbm range, SDF inside/outside,
  trace hit/miss, palette ends, unit normals, render size/background/determinism, PPM header).

## What I learned

- **An explosion is a sphere plus noise.** Subtracting fractal noise from the radius in a signed distance
  function gives a lumpy, cloud-like surface with no mesh at all.
- **Value noise** is a hashed lattice interpolated trilinearly; smoothstep on the fractions removes visible
  grid creases. **fBm** sums octaves at doubling-ish frequencies and halving amplitudes; rotating each octave
  hides axis alignment.
- **Sphere tracing** steps along the ray by the distance to the surface. A displaced SDF is not a true
  distance, so stepping by a fraction (0.1x) avoids overshooting into the surface.
- **A bounding-sphere test first** rejects background pixels cheaply and also gives the march a clean exit.
- **Normals come from the field itself** via central differences of the SDF; the same noise value picks the
  color from the fire palette (deep valleys are hot), so shape and color stay coupled.
- Testing a renderer: assert properties (ranges, determinism, hit/miss, unit normals) rather than pixels.

## Known limits

- Static frame only; the original animates by sweeping the noise amplitude across frames.
- Single-threaded, ~1.4 s for 320x240 at -O2; no anti-aliasing.
- `M_PI` is used, which is POSIX rather than strict ISO C++.
