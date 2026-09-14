#include "matmul.hpp"
#include "micro_kernel.hpp"

#include <algorithm>
#include <cstring>

// matmul_simd loads a fresh 8-wide slice of B for every single row of A, so
// each B value gets read from memory once per output row. Register blocking
// fixes that: hold MR=4 rows of A's worth of accumulators live in registers
// at once and share every B load across all 4, cutting B's traffic 4x for
// the same arithmetic.
void matmul_kernel(const float* A, const float* B, float* C, size_t M, size_t K, size_t N) {
    std::memset(C, 0, sizeof(float) * M * N);
    using detail::accumulate_tile;
    using detail::MR;

    const size_t n8 = N - (N % 8);

    for (size_t i = 0; i < M; i += MR) {
        const size_t rows = std::min(MR, M - i);

        size_t j = 0;
        for (; j < n8; j += 8) {
            accumulate_tile(A, B, C, K, N, i, j, /*k0=*/0, /*kc=*/K, rows);
        }
        // Fewer than 8 columns left: scalar cleanup, one row at a time.
        for (size_t r = 0; r < rows; ++r) {
            for (size_t jj = j; jj < N; ++jj) {
                float sum = 0.0f;
                for (size_t k = 0; k < K; ++k) {
                    sum += A[(i + r) * K + k] * B[k * N + jj];
                }
                C[(i + r) * N + jj] = sum;
            }
        }
    }
}
