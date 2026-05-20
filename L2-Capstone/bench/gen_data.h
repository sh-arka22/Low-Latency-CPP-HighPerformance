#pragma once

#include "common.h"
#include <cstdint>

namespace bench {

struct GenericTick {
    double   price;
    uint64_t timestamp_ns;
    uint32_t qty;
    common::Side    side;
    common::VenueID venue;
};

void generate_ticks(GenericTick* out, uint32_t n, uint64_t seed = 42);

}
