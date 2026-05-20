#include "fast.h"

namespace fast {

























template <common::Side S>
[[gnu::always_inline]] inline
void process_one(/* TODO: figure out the args */) noexcept {

}

uint64_t run_fast(const bench::GenericTick* ticks, uint32_t n) {



    uint64_t checksum = 0;

    for (uint32_t i = 0; i < n; ++i) {
        const auto& t = ticks[i];


        (void)t;
        (void)checksum;
    }

    return checksum;
}

}
