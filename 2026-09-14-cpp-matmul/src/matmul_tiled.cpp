#include "matmul.hpp"
#include "micro_kernel.hpp"

#include <algorithm>
#include <cstring>

namespace {
// KC*NC worth of B (plus the C tile being updated) is what this stage is
// trying to keep resident in L2 across the full sweep over M -- both chosen
// well under a typical 256KB-1MB L2, with room to spare for A and C.
constexpr size_t KC = 256;
constexpr size_t NC = 512;
}  // namespace

// matmul_kernel already reuses each B load MR=4 ways, but it still streams
// all of B and C through the CPU once per full pass over K -- for a large
// matrix, that panel of B has fallen out of cache long before the loop over
// M comes back around to it. Blocking K and N into panels (KC x NC) and
// finishing a panel's contribution to every row of C before moving to the
// next panel keeps that panel of B cache-resident for the whole M sweep.
void matmul_tiled(const float* A, const float* B, float* C, size_t M, size_t K, size_t N) {
    std::memset(C, 0, sizeof(float) * M * N);
    using detail::accumulate_tile;
    using detail::MR;

    for (size_t k0 = 0; k0 < K; k0 += KC) {
        const size_t kc = std::min(KC, K - k0);
        for (size_t j0 = 0; j0 < N; j0 += NC) {
            const size_t nc = std::min(NC, N - j0);
            const size_t nc8 = nc - (nc % 8);

            for (size_t i = 0; i < M; i += MR) {
                const size_t rows = std::min(MR, M - i);

                size_t jj = 0;
                for (; jj < nc8; jj += 8) {
                    accumulate_tile(A, B, C, K, N, i, j0 + jj, k0, kc, rows);
                }
                // Fewer than 8 columns left in this panel: scalar cleanup,
                // restricted to this panel's own k range -- but C already
                // holds every earlier k-panel's contribution (or 0, on the
                // first one, since C was memset up front), so this must
                // accumulate onto it rather than overwrite.
                for (size_t r = 0; r < rows; ++r) {
                    for (size_t j = j0 + jj; j < j0 + nc; ++j) {
                        float sum = C[(i + r) * N + j];
                        for (size_t k = k0; k < k0 + kc; ++k) {
                            sum += A[(i + r) * K + k] * B[k * N + j];
                        }
                        C[(i + r) * N + j] = sum;
                    }
                }
            }
        }
    }
}
