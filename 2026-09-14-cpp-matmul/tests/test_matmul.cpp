// Hand-rolled test harness -- no GoogleTest/Catch2 install, matching this
// repo's no-install-needed convention (see 2026-08-25-cpp-tiny-renderer,
// 2026-08-08-cpp-raytracer).

#include "matmul.hpp"

#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

namespace {

int g_passed = 0;
int g_failed = 0;

void check(bool cond, const std::string& name) {
    if (cond) {
        g_passed++;
    } else {
        g_failed++;
        std::printf("FAIL: %s\n", name.c_str());
    }
}

using MatmulFn = void (*)(const float*, const float*, float*, size_t, size_t, size_t);

// Every optimized stage is checked against matmul_naive on the same random
// input, at a set of shapes deliberately chosen to stress the seams between
// full SIMD/register tiles and the scalar cleanup that handles what's left
// over: exact multiples of 8/MR, one short, one over, and non-square M/K/N
// so a bug that only shows up when M != N != K (or K spans multiple KC
// blocks) can't hide.
bool matches_naive(MatmulFn fn, size_t M, size_t K, size_t N, const std::string& label) {
    std::mt19937 rng(12345);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

    std::vector<float> A(M * K), B(K * N), C_ref(M * N), C_got(M * N);
    for (auto& v : A) v = dist(rng);
    for (auto& v : B) v = dist(rng);

    matmul_naive(A.data(), B.data(), C_ref.data(), M, K, N);
    fn(A.data(), B.data(), C_got.data(), M, K, N);

    float max_abs_diff = 0.0f;
    for (size_t i = 0; i < M * N; ++i) {
        max_abs_diff = std::max(max_abs_diff, std::fabs(C_ref[i] - C_got[i]));
    }
    // Single precision, K-deep dot products: tolerance scales with K, not a
    // bare epsilon, or this starts failing on shape alone at large K.
    const float tol = 1e-3f * static_cast<float>(K);
    bool ok = max_abs_diff <= tol;
    check(ok, label + " matches naive at " + std::to_string(M) + "x" + std::to_string(K) + "x" +
                  std::to_string(N) + " (max abs diff " + std::to_string(max_abs_diff) + ")");
    return ok;
}

void test_stage(MatmulFn fn, const std::string& label) {
    matches_naive(fn, 1, 1, 1, label);      // degenerate: 1x1x1
    matches_naive(fn, 8, 8, 8, label);      // exact SIMD width, exact MR
    matches_naive(fn, 5, 7, 3, label);      // smaller than one tile in every dim
    matches_naive(fn, 13, 17, 11, label);   // odd sizes, no dimension a multiple of 4 or 8
    matches_naive(fn, 64, 64, 64, label);   // exact multiples of MR and 8
    matches_naive(fn, 67, 64, 68, label);   // M and N one past an exact multiple
    matches_naive(fn, 300, 300, 300, label);  // K spans multiple KC=256 tiled blocks
    matches_naive(fn, 130, 260, 137, label);  // non-square, N tail not a multiple of 8
}

void test_naive_self_consistent() {
    // Sanity check on the harness itself: naive vs. naive must be exact
    // (same instruction order, same rounding), tolerance or not.
    check(matches_naive(matmul_naive, 4, 4, 4, "naive"), "naive vs naive baseline sanity");
}

}  // namespace

int main() {
    test_naive_self_consistent();
    test_stage(matmul_ikj, "ikj");
    test_stage(matmul_simd, "simd");
    test_stage(matmul_kernel, "kernel");
    test_stage(matmul_tiled, "tiled");

    std::printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
