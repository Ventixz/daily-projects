#pragma once

// Shared inner loop for matmul_kernel and matmul_tiled. Kept in one place
// because both stages need the exact same register-blocked accumulate, and
// having two hand-written copies of AVX2 intrinsics is how they'd quietly
// drift apart.

#include <immintrin.h>

#include <cstddef>

namespace detail {

// Rows of A (and of C) advanced together so each 8-wide load from B is
// reused MR times instead of once -- the register-blocking trick.
constexpr size_t MR = 4;

// Accumulates C[i0+r][j0..j0+8) += sum_{k=k0}^{k0+kc-1} A[i0+r][k]*B[k][j0..j0+8)
// for r in [0, rows), rows <= MR. Always a read-modify-write on C, so the
// caller decides what "starting from zero" means (memset before the first
// call, or just always seeding from C once it's been zeroed once).
inline void accumulate_tile(const float* A, const float* B, float* C, size_t K, size_t N,
                             size_t i0, size_t j0, size_t k0, size_t kc, size_t rows) {
    __m256 acc[MR];
    for (size_t r = 0; r < rows; ++r) {
        acc[r] = _mm256_loadu_ps(C + (i0 + r) * N + j0);
    }
    for (size_t k = k0; k < k0 + kc; ++k) {
        const __m256 b_vec = _mm256_loadu_ps(B + k * N + j0);
        for (size_t r = 0; r < rows; ++r) {
            acc[r] = _mm256_fmadd_ps(_mm256_set1_ps(A[(i0 + r) * K + k]), b_vec, acc[r]);
        }
    }
    for (size_t r = 0; r < rows; ++r) {
        _mm256_storeu_ps(C + (i0 + r) * N + j0, acc[r]);
    }
}

}  // namespace detail
