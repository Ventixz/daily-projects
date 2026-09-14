#include "matmul.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using MatmulFn = void (*)(const float*, const float*, float*, size_t, size_t, size_t);

double gflops(size_t M, size_t K, size_t N, double seconds) {
    const double flops = 2.0 * static_cast<double>(M) * static_cast<double>(K) * static_cast<double>(N);
    return flops / seconds / 1e9;
}

void bench(const char* name, MatmulFn fn, const std::vector<float>& A, const std::vector<float>& B,
           std::vector<float>& C, size_t M, size_t K, size_t N, int reps) {
    // One untimed warm-up call so the first timed rep isn't paying for cold
    // instruction cache / branch predictor state that every later rep gets
    // for free.
    fn(A.data(), B.data(), C.data(), M, K, N);

    const auto start = Clock::now();
    for (int r = 0; r < reps; ++r) {
        fn(A.data(), B.data(), C.data(), M, K, N);
    }
    const auto end = Clock::now();
    const double seconds = std::chrono::duration<double>(end - start).count() / reps;

    std::printf("%-8s %8.1f ms   %8.2f GFLOP/s\n", name, seconds * 1000.0, gflops(M, K, N, seconds));
}

}  // namespace

int main(int argc, char** argv) {
    size_t N = 512;
    if (argc > 1) N = static_cast<size_t>(std::atoi(argv[1]));
    const size_t M = N, K = N;

    std::printf("matmul benchmark: %zux%zu * %zux%zu\n\n", M, K, K, N);

    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    std::vector<float> A(M * K), B(K * N), C(M * N);
    for (auto& v : A) v = dist(rng);
    for (auto& v : B) v = dist(rng);

    // Correctness gate before benchmarking anything: a fast wrong answer
    // isn't a result worth timing.
    std::vector<float> ref(M * N);
    matmul_naive(A.data(), B.data(), ref.data(), M, K, N);
    struct Stage {
        const char* name;
        MatmulFn fn;
    };
    const Stage stages[] = {
        {"naive", matmul_naive}, {"ikj", matmul_ikj}, {"simd", matmul_simd},
        {"kernel", matmul_kernel}, {"tiled", matmul_tiled},
    };
    for (const auto& stage : stages) {
        stage.fn(A.data(), B.data(), C.data(), M, K, N);
        float max_abs_diff = 0.0f;
        for (size_t i = 0; i < M * N; ++i) max_abs_diff = std::max(max_abs_diff, std::fabs(ref[i] - C[i]));
        if (max_abs_diff > 1e-3f * static_cast<float>(K)) {
            std::printf("%-8s DISAGREES with naive (max abs diff %f) -- refusing to benchmark it\n",
                        stage.name, max_abs_diff);
            return 1;
        }
    }

    // N=512 keeps the naive stage's O(N^3) around a couple seconds even at
    // reps=1; the optimized stages get a handful of reps so timing noise
    // averages out.
    std::printf("%-8s %8s   %8s\n", "stage", "time", "throughput");
    bench("naive", matmul_naive, A, B, C, M, K, N, 1);
    bench("ikj", matmul_ikj, A, B, C, M, K, N, 3);
    bench("simd", matmul_simd, A, B, C, M, K, N, 5);
    bench("kernel", matmul_kernel, A, B, C, M, K, N, 5);
    bench("tiled", matmul_tiled, A, B, C, M, K, N, 5);
    return 0;
}
