#include "matmul.hpp"

#include <cstring>

void matmul_naive(const float* A, const float* B, float* C, size_t M, size_t K, size_t N) {
    std::memset(C, 0, sizeof(float) * M * N);
    for (size_t i = 0; i < M; ++i) {
        for (size_t j = 0; j < N; ++j) {
            float sum = 0.0f;
            for (size_t k = 0; k < K; ++k) {
                sum += A[i * K + k] * B[k * N + j];
            }
            C[i * N + j] = sum;
        }
    }
}
