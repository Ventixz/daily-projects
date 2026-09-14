#include "matmul.hpp"

#include <cstring>

// Same arithmetic as matmul_naive, loops reordered i-k-j. The inner loop now
// walks B and C row-wise (stride 1) instead of column-wise (stride N), which
// is what actually costs the naive version its cache misses -- nothing about
// the math changes, only the order sums get accumulated in.
void matmul_ikj(const float* A, const float* B, float* C, size_t M, size_t K, size_t N) {
    std::memset(C, 0, sizeof(float) * M * N);
    for (size_t i = 0; i < M; ++i) {
        const float* a_row = A + i * K;
        float* c_row = C + i * N;
        for (size_t k = 0; k < K; ++k) {
            const float a_ik = a_row[k];
            const float* b_row = B + k * N;
            for (size_t j = 0; j < N; ++j) {
                c_row[j] += a_ik * b_row[j];
            }
        }
    }
}
