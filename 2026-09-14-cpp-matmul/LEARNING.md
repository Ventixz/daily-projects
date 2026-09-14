# High-Performance Matrix Multiplication

**Source:** ["High-Performance Matrix Multiplication"](https://gist.github.com/nadavrot/5b35d44e8ba3dd718e595e40184d03f0)
by Nadav Rotem, one of the C/C++ entries in
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).

Built end-to-end in one sitting. No external dependencies — just AVX2/FMA intrinsics
(`<immintrin.h>`) and the standard library.

## What it is

Five implementations of `C = A * B` for general (non-square) dense `float` matrices,
each one a small, targeted fix to the previous stage's actual bottleneck rather than a
rewrite:

- `src/matmul_naive.cpp` — textbook triple loop, `i-j-k` order.
- `src/matmul_ikj.cpp` — same arithmetic, loops reordered to `i-k-j` so the inner loop
  walks B and C row-wise instead of column-wise.
- `src/matmul_simd.cpp` — `ikj` plus hand-written AVX2: broadcast `A[i][k]`, FMA it
  against 8 lanes of B into 8 lanes of C at once.
- `src/matmul_kernel.cpp` — adds register blocking via `src/micro_kernel.hpp`'s
  `accumulate_tile`: 4 rows of A share every 8-wide load from B instead of each row
  reloading it, cutting B's memory traffic 4x for the same FLOPs.
- `src/matmul_tiled.cpp` — adds cache blocking on top of the same micro-kernel: sweep
  `KC x NC` panels of B to completion (every row of C gets its contribution from that
  panel) before moving to the next panel, instead of streaming all of B once per
  register-blocked pass over K.

`micro_kernel.hpp` is the one piece of code both `matmul_kernel.cpp` and
`matmul_tiled.cpp` call into — the register-blocked accumulate is fiddly enough
(explicit `__m256` arrays, loop-carried FMAs) that writing it twice would have meant
two chances to get it subtly wrong instead of one.

## Run it

```bash
cd 2026-09-14-cpp-matmul
make test                    # 34 correctness checks, every stage vs. matmul_naive
make run                     # benchmark at 512x512x512
./bin/matmul_bench 1024      # or any size, as the one CLI argument
```

## What it actually teaches

- **`-O3` auto-vectorizes the obvious case, so "SIMD" isn't the win you think it is
  until you also fix the algorithm.** `matmul_ikj` is plain scalar C++; `matmul_simd`
  hand-writes the exact same broadcast-and-FMA pattern with `<immintrin.h>`. Checking
  the generated assembly (`g++ -O3 -S`) shows GCC already emits `vbroadcastss` +
  `vfmadd213ps` on **8-wide `ymm` registers** for the plain `ikj` loop — it's already
  auto-vectorized. Benchmark numbers confirm it: `ikj` and `simd` land within noise of
  each other at every size tested (512³: 21.0 vs 20.9 GFLOP/s; 1024³: 11.7 vs 11.9). The
  loop was simple and stride-1 enough for the compiler to do exactly what I would have
  hand-written. The lesson isn't "don't bother with intrinsics" — it's that intrinsics
  only pay for themselves once you're doing something the auto-vectorizer can't
  express, which is exactly what register blocking is (see next point).

- **Register blocking can make things *worse* once the matrix stops fitting in cache —
  and it's a straight regression, not just a smaller win.** At 512³, `matmul_kernel`
  (register-blocked) comfortably beats `matmul_simd` (43.2 vs 20.9 GFLOP/s). At 1024³,
  where B alone is 4 MB, `matmul_kernel` *drops below* `matmul_simd` (7.6 vs 11.9
  GFLOP/s) — worse than the version it's supposed to improve on. Register blocking
  only changes how many times each loaded value gets *reused in registers*; it does
  nothing about how many times the *same panel of B* gets re-streamed from DRAM as the
  outer loop sweeps back over M. `matmul_tiled` adds exactly that fix — bound the K/N
  panel of B in flight to `KC x NC` (256 x 512 floats, chosen to comfortably fit L2)
  and finish every row of C's contribution from that panel before advancing — and gets
  37.6 GFLOP/s at the same 1024³ size where the un-tiled kernel had regressed. Measuring
  at only one matrix size would have hidden this completely; the whole point only shows
  up once the working set crosses a cache boundary.

- **A tiled accumulator has a real "which iteration am I on" bug, and a single K size
  can't catch it.** The scalar cleanup path in `matmul_tiled` (for the `< 8` leftover
  columns in a panel) initially read `float sum = 0.0f;` before summing that panel's own
  `k0..k0+kc` range — copied verbatim from `matmul_kernel`'s cleanup loop, where it's
  correct because that loop always covers the *entire* K range in one call. In the
  tiled version, C already holds every earlier panel's partial sum by the time a later
  `k0` block runs (or exactly 0, on the first block, since `memset` zeroed it up front)
  — so zeroing `sum` silently threw away every prior panel's contribution to those
  columns. `make test` caught it immediately, but only on the two shapes that actually
  span more than one `KC=256` block (`300x300x300` and `130x260x137`); every shape
  ≤ 256 in K passed regardless, because there was only ever one panel to "accumulate
  onto." The fix was one line — seed from `C[(i+r)*N+j]` instead of `0.0f`, which is
  correct on the first block too since C starts at zero there anyway — but finding it
  required a test matrix specifically sized to exercise the seam between two panels,
  not just "a few random shapes."

- **Tolerance has to scale with the reduction depth, not be a flat epsilon.** Every
  correctness check compares against `matmul_naive` with `tol = 1e-3 * K`, not a fixed
  `1e-4`. Reordering a 512-deep `float` dot product's additions (which every non-naive
  stage does) changes the rounding error accumulated along the way, and that error
  grows with how many terms get summed — a flat epsilon either false-fails on deep
  reductions or is too loose to catch a real bug on shallow ones.

## Known limitations (by design, to keep scope to ~2-4 hours)

- Single-threaded. The tutorial's ~100 GFLOP/s target assumes multi-core; this stops at
  the single-thread cache-blocking story, which was already enough to hit two genuine,
  non-obvious findings above.
- One fixed register-tile shape (4 rows x 8 columns) and one fixed cache-tile shape
  (256 x 512) — not auto-tuned per-CPU the way a real BLAS (OpenBLAS, MKL) picks tile
  sizes from detected cache sizes.
- `float` only; no packing/repacking of A or B into a cache-friendlier layout before
  the tiled pass, which real GEMM kernels use to make the inner loop's memory access
  fully sequential regardless of the input's stride.

## License

Licensed under the MIT License; see the LICENSE file at the repository root.
Credit: ["High-Performance Matrix Multiplication"](https://gist.github.com/nadavrot/5b35d44e8ba3dd718e595e40184d03f0)
by Nadav Rotem.
