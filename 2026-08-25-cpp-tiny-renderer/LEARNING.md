# Tiny Renderer, or How OpenGL Works (C++)

**Source:** [Tiny Renderer or how OpenGL works: software rendering in 500
lines of code](https://github.com/ssloy/tinyrenderer/wiki) by Dmitry
Sokolov (Lessons 0–3: line drawing, triangle rasterization with back-face
culling, and the z-buffer), from the C/C++ section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).

Built end-to-end in one sitting, so this folder contains the finished
implementation directly at the project root (no separate `reference/`).
Zero external dependencies — standard library only, own PPM writer instead
of the tutorial's TGA, own OBJ loader, own barycentric-coordinate
derivation (the dot-product/linear-algebra form, not the tutorial's
cross-product shortcut).

## What it is

- `include/geometry.hpp` — `Vec3f` (points, directions, normals, and
  colors are all the same shape) plus `Vec2i` for pixel coordinates and a
  plain `Color` struct.
- `src/model.cpp` — a position-only Wavefront `.obj` loader: `v x y z` and
  `f a b c` lines only, tolerant of `f a/vt/vn` texture/normal indices (it
  just takes the field before the first `/`) and silent about everything
  else (comments, `vt`, `vn`, group tags). `face_normal()` computes the
  outward normal via `(v1-v0) × (v2-v0)`.
- `src/render.cpp` — `line()` (Bresenham, with the steep-line transpose),
  `barycentric()` (solved via the standard 2×2 linear system over edge
  vectors, not the cross-product trick tinyrenderer uses), `triangle()`
  (bounding-box scan + barycentric membership test, with an optional
  `ZBuffer*` for per-pixel depth testing), and `ZBuffer` itself.
- `src/main.cpp` — renders four PPMs from three hand-authored cube models,
  under a fixed camera looking down `+z` from `z = -∞` (so smaller `z` is
  nearer) and a headlamp light co-located with it:
  - `wireframe.ppm` — `cube_solo.obj`'s edges only, via `line()`.
  - `shaded_cube.ppm` — the same cube, filled and flat-shaded, one visible
    quad culled by depth alone (see below).
  - `overlap_no_zbuffer.ppm` / `overlap_zbuffer.ppm` — `cube_near.obj` (red,
    closer, smaller) and `cube_far.obj` (blue, farther, bigger, deliberately
    overlapping `cube_near` in screen space), drawn in the same
    near-then-far order with and without a `ZBuffer`.
- `models/*.obj` — three cubes, hand-derived (not downloaded): 8 vertices
  and 12 CCW-outward-wound triangles each, verified by hand via the
  cross-product for every one of the cube's six faces before being typed
  in (see the comments at the top of `render.hpp`/`model.hpp` for the
  convention this depends on).
- `tools/ppm_to_png.py` — carried over unchanged from
  `2026-08-08-cpp-raytracer/tools/`, same repo, same author.
- `tests/test_render.cpp` — 34 hand-rolled tests (no GoogleTest/Catch2
  install): vector algebra, OBJ parsing (including the slash-qualified
  face-index case and an empty-file failure case), face-normal winding
  (including the degenerate zero-area case), line-drawing pixel counts and
  the reverse-order symmetry property, barycentric coordinates at the
  vertices/centroid/outside/degenerate, triangle fill bounds, and — the
  centerpiece — z-buffer depth resolution under both draw orders.

## Run it

```bash
cd 2026-08-25-cpp-tiny-renderer
make test     # 34 tests
make run      # writes renders/*.ppm
python3 tools/ppm_to_png.py renders/wireframe.ppm renders/wireframe.png
python3 tools/ppm_to_png.py renders/shaded_cube.ppm renders/shaded_cube.png
python3 tools/ppm_to_png.py renders/overlap_no_zbuffer.ppm renders/overlap_no_zbuffer.png
python3 tools/ppm_to_png.py renders/overlap_zbuffer.ppm renders/overlap_zbuffer.png
```

Actual output:

```
$ make test
g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 -Iinclude tests/test_render.cpp src/model.cpp src/render.cpp src/image.cpp -o bin/test_tiny_renderer
./bin/test_tiny_renderer
34 passed, 0 failed

$ make run
./bin/tiny_renderer
wrote renders/wireframe.ppm, shaded_cube.ppm, overlap_no_zbuffer.ppm, overlap_zbuffer.ppm
```

`renders/wireframe.png` is a plain square with one diagonal — not a bug:
viewed dead-on along the axis this orthographic camera uses, the cube's
near face, far face, and four side faces all project onto exactly the
same square outline, so the only edge that shows through distinctly is the
near quad's own triangulation diagonal.

`renders/shaded_cube.png` is a single flat-white square: the near face's
normal points straight back at the headlamp (co-located with the camera),
so `intensity = normal · light_dir = 1.0` uniformly across it — flat
shading has no per-pixel variation within one triangle by construction.

`renders/overlap_no_zbuffer.png` and `renders/overlap_zbuffer.png` are the
two frames that actually demonstrate the point of this project — see
below.

## What it actually teaches

- **A line-drawing loop that steps one pixel per column silently breaks
  the moment a line is taller than it is wide.** `test_line_drawing`'s
  steep-line case (`{2,2}` to `{5,12}`: 10 rows across only 3 columns) is
  exactly the shape that exposes it — stepping `x` from 2 to 5 visits four
  columns, so a naive loop draws at most 4 pixels while the line is
  visually 11 rows tall, leaving 7 rows of gap. `line()`'s fix is to
  detect `|dx| < |dy|` and **transpose**: walk the long axis as if it were
  `x`, and swap back only when writing each pixel. The test asserts the
  count directly (`dy + 1 == 11` pixels lit), which a "looks about right"
  visual check wouldn't have caught.
- **Bresenham's algorithm isn't symmetric by accident — the left-to-right
  normalization is what makes it symmetric.** Before sorting endpoints,
  `line(a, b)` and `line(b, a)` can choose different rounding at each
  step and light slightly different pixels for the same on-screen
  segment. Sorting so `x0 <= x1` (or the transposed axis) always walks the
  same direction regardless of which endpoint the caller passed first;
  `test_line_drawing`'s explicit `line(p0,p1)` vs `line(p1,p0)` comparison
  is what pins this down instead of leaving it as an unstated assumption.
- **Back-face culling is a single dot product, but the sign convention
  has to match the camera's actual direction or every face renders
  inside-out.** This project's camera looks down `+z`; a face is toward
  the viewer only if its outward normal has a *negative* component along
  `+z`, i.e. `normal · view_dir < 0`. Getting the inequality backwards
  doesn't crash — it silently renders the inside of the model instead of
  the outside, which looks plausible right up until two objects overlap
  (see the z-buffer point below) or the camera moves (out of scope here,
  but the bug would only show up then). `test_face_normal`'s
  reversed-winding case exists because this sign convention has nothing to
  anchor it except "did I wind the model file the way I think I did."
- **Filling triangles independently, even with correct per-triangle
  culling, still gets multi-object depth wrong — because culling only
  answers "is this triangle facing me," never "is something else in front
  of it."** `overlap_no_zbuffer.png` is the concrete demonstration:
  `cube_near` (red) and `cube_far` (blue) are each drawn with correct
  back-face culling, in the deliberately "wrong" order (near first, far
  second). Where their footprints overlap, blue simply overwrites red —
  the farther object visibly appears in front, purely because it happened
  to be drawn last. `test_zbuffer_picks_nearer_surface`'s first block
  asserts this exact failure (`img.get(5,5).b == 255`, meaning "far
  wins") before the fix is even introduced, the same way the search-engine
  project's tf-idf test showed a regression before proposing the log-damped
  fix.
- **A z-buffer converts "who was drawn last" into "who is actually
  closer," and once it's there, draw order stops mattering at all.**
  `ZBuffer::test_and_set` only lets a pixel write through if its
  interpolated `z` beats what's already stored there — initialized to
  `+∞` so the very first triangle to touch any pixel always passes.
  `test_zbuffer_picks_nearer_surface`'s second and third blocks render the
  *identical* two triangles in both orders (near-then-far, far-then-near)
  and check the same pixel comes out red either way — `overlap_zbuffer.png`
  is that same guarantee, visually: red now correctly shows through
  everywhere its cube is nearer, even in the region blue would otherwise
  have won.
- **Barycentric coordinates don't require the cross-product shortcut
  tinyrenderer teaches — solving the 2×2 linear system directly (Cramer's
  rule over the edge-vector dot products) gives the identical weights**,
  and is arguably easier to verify independently: `test_barycentric`
  checks the three vertices come back as `(1,0,0)`/`(0,1,0)`/`(0,0,1)`
  and the centroid as `(1/3,1/3,1/3)` by construction of the formula, not
  by comparison against the tutorial's derivation.
- **A degenerate (zero-area) triangle has to resolve to "no valid
  weights," not a division by a zero denominator.** `barycentric()`
  checks `|denom| < 1e-8` before dividing and returns `{-1,-1,-1}` —
  guaranteed to fail every membership test downstream — exactly mirroring
  `face_normal()`'s zero-vector return for a degenerate face. Both
  `test_face_normal` and `test_barycentric` assert this directly rather
  than trusting that "it'll just happen to not blow up."

## Deliberate scope cuts

- **Lessons 0–3 only.** No perspective projection (lesson 4, would need a
  real projection/view matrix instead of the direct `x,y → pixel` mapping
  used here), no camera movement (lesson 5), no shaders, texturing, or
  Gouraud/Phong per-vertex shading (lesson 6 onward) — every triangle in
  this project is flat-shaded, one color per face.
- **Three hand-authored cubes, not a downloaded model.** The classic
  tinyrenderer tutorial renders a ~1300-triangle African head OBJ; this
  project uses three 12-triangle cubes instead, built specifically so
  their overlap (or lack of it) is easy to reason about and to pin down in
  a unit test. `Model::load()` itself is tested separately against
  synthetic in-memory OBJ text, so it isn't specific to these three files.
- **PPM output, not TGA.** Functionally equivalent for this project's
  purposes (a flat, uncompressed pixel dump); PPM's header is a few bytes
  of ASCII versus TGA's fixed 18-byte binary header, and this project
  doesn't need TGA's alpha channel or RLE compression.
- **No texture mapping or vertex normals.** The OBJ loader only reads
  vertex positions; `vt`/`vn` lines and the corresponding face-index
  fields are read (to stay parse-compatible with real exported OBJ files)
  but never used.

## What I'd add next

- **Perspective projection** (lesson 4), replacing the direct `(x+1)*w/2`
  mapping with a real projection matrix, so the camera could move without
  every object's apparent size staying constant regardless of distance.
- **Per-vertex normals and Gouraud shading**, interpolating shading across
  a triangle via the same barycentric weights `triangle()` already
  computes for the z-test, instead of one flat color per face.
- **Loading a real multi-thousand-triangle OBJ model** (the classic
  African head, or any textured mesh) now that the loader, rasterizer, and
  z-buffer have all been validated independently against small, exact
  fixtures.

## License

Licensed under the MIT License; see the LICENSE file at the repository
root. Built from ["Tiny Renderer or how OpenGL
works"](https://github.com/ssloy/tinyrenderer/wiki) by Dmitry Sokolov.
