#include "matmul.hpp"

#include <cstring>
#include <immintrin.h>

// matmul_ikj, but the inner j loop updates 8 columns of C per instruction
// with AVX2: broadcast the single scalar A[i][k] into a vector register, and
// FMA it against 8 lanes of B's row directly into 8 lanes of C's row. B is
// read once and used for 8 outputs instead of once per output.
void matmul_simd(const float* A, const float* B, float* C, size_t M, size_t K, size_t N) {
    std::memset(C, 0, sizeof(float) * M * N);
    const size_t n8 = N - (N % 8);

    for (size_t i = 0; i < M; ++i) {
        const float* a_row = A + i * K;
        float* c_row = C + i * N;
        for (size_t k = 0; k < K; ++k) {
            const __m256 a_ik = _mm256_set1_ps(a_row[k]);
            const float* b_row = B + k * N;

            size_t j = 0;
            for (; j < n8; j += 8) {
                __m256 c_vec = _mm256_loadu_ps(c_row + j);
                __m256 b_vec = _mm256_loadu_ps(b_row + j);
                c_vec = _mm256_fmadd_ps(a_ik, b_vec, c_vec);
                _mm256_storeu_ps(c_row + j, c_vec);
            }
            // Fewer than 8 columns left over: plain scalar FMA.
            for (; j < N; ++j) {
                c_row[j] += a_row[k] * b_row[j];
            }
        }
    }
}
