#pragma once

#include <cstddef>

// C = A * B for general (non-square) dense matrices, all row-major and
// contiguous with no padding: A is M x K, B is K x N, C is M x N.
// Every implementation zeroes C itself, so callers pass an uninitialized
// (but allocated) output buffer.
//
// Five stages, each building on the last, matching the progression in
// https://gist.github.com/nadavrot/5b35d44e8ba3dd718e595e40184d03f0:
//
//   naive    - textbook triple loop, ijk order
//   ikj      - same math, loop order swapped for sequential memory access
//   simd     - ikj plus AVX2: 8 columns of C updated per FMA
//   kernel   - simd plus register blocking: 4 rows of A share each B load
//   tiled    - kernel plus cache blocking over K and N

void matmul_naive(const float* A, const float* B, float* C, size_t M, size_t K, size_t N);
void matmul_ikj(const float* A, const float* B, float* C, size_t M, size_t K, size_t N);
void matmul_simd(const float* A, const float* B, float* C, size_t M, size_t K, size_t N);
void matmul_kernel(const float* A, const float* B, float* C, size_t M, size_t K, size_t N);
void matmul_tiled(const float* A, const float* B, float* C, size_t M, size_t K, size_t N);
