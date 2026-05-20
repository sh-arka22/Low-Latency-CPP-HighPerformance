#include "gen_data.h"

namespace bench {

namespace {

[[gnu::always_inline]] inline uint64_t xorshift64(uint64_t& s) noexcept {
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    return s;
}

}

void generate_ticks(GenericTick* out, uint32_t n, uint64_t seed) {
    uint64_t s = seed ? seed : 1;
    uint64_t ts = 1'000'000'000ULL;
    for (uint32_t i = 0; i < n; ++i) {
        uint64_t r = xorshift64(s);

        double price;
        if ((r & 0x3F) == 0) {
            price = (r & 1) ? -1.0 : (common::MAX_PRICE + 100.0);
        } else {
            price = 50.0 + double((r >> 8) & 0xFFFF) * 0.01;
        }

        uint32_t qty = uint32_t((r >> 24) & 0xFFFF);
        if ((r & 0x7F) == 0) qty = 0;

        common::Side    side  = common::Side(((r >> 40) & 1));
        common::VenueID venue = common::VenueID(((r >> 41) & 0x3));

        ts += 50 + (r & 0x3F);

        out[i] = GenericTick{ price, ts, qty, side, venue };
    }
}

}
