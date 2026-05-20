#include "../include/common.h"
#include "../bench/gen_data.h"
#include "../src/naive.h"
#include "../src/fast.h"

#include <chrono>
#include <cstdio>
#include <vector>

int main() {
    using namespace std::chrono;
    using common::NUM_TICKS;

    std::vector<bench::GenericTick> ticks(NUM_TICKS);
    bench::generate_ticks(ticks.data(), NUM_TICKS);

    std::printf("Generated %u ticks.\n\n", NUM_TICKS);
    std::printf("sizeof(naive::Tick)  = %zu bytes\n", sizeof(naive::Tick));
    std::printf("sizeof(naive::Quote) = %zu bytes  (alignof = %zu)\n",
                sizeof(naive::Quote), alignof(naive::Quote));
    std::printf("sizeof(fast::Tick)   = %zu bytes\n", sizeof(fast::Tick));
    std::printf("sizeof(fast::Quote)  = %zu bytes  (alignof = %zu)\n\n",
                sizeof(fast::Quote), alignof(fast::Quote));

    uint64_t c_n = 0, t0, t1;

    t0 = common::rdtsc();
    for (int rep = 0; rep < 3; ++rep)
        c_n ^= naive::run_naive(ticks.data(), NUM_TICKS);
    t1 = common::rdtsc();
    double cyc_naive = double(t1 - t0) / (3.0 * NUM_TICKS);

    uint64_t c_f = 0;
    t0 = common::rdtsc();
    for (int rep = 0; rep < 3; ++rep)
        c_f ^= fast::run_fast(ticks.data(), NUM_TICKS);
    t1 = common::rdtsc();
    double cyc_fast = double(t1 - t0) / (3.0 * NUM_TICKS);

    std::printf("NAIVE:   cycles/tick = %7.2f   checksum = 0x%016lx\n",
                cyc_naive, c_n);
    std::printf("FAST:    cycles/tick = %7.2f   checksum = 0x%016lx\n",
                cyc_fast, c_f);
    std::printf("SPEEDUP: %.2fx\n", cyc_naive / cyc_fast);

    if (c_n != c_f) {
        std::printf("\n*** CHECKSUM MISMATCH — fast version diverges from naive ***\n");
        return 1;
    }
    if (cyc_fast * 4.0 > cyc_naive) {
        std::printf("\n*** Speedup below 4x — apply more rules ***\n");
        return 2;
    }
    std::printf("\nPASSED.\n");
    return 0;
}
