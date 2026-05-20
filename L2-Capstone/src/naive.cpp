#include "naive.h"
#include <vector>
#include <cstring>

namespace naive {



















uint64_t run_naive(const bench::GenericTick* ticks, uint32_t n) {



    uint64_t checksum = 0;





    for (uint32_t i = 0; i < n; ++i) {
        const auto& t = ticks[i];








        (void)t;
        (void)checksum;
    }

    return checksum;
}

}
