#pragma once

#include "../include/common.h"
#include "../bench/gen_data.h"
#include <cstdint>

namespace fast {

struct Tick {

};

static_assert(sizeof(Tick) <= 32, "Tick must fit 2-per-cache-line");

struct alignas(64) Quote {
    double   best_bid;
    double   best_ask;
    uint64_t bid_ts;
    uint64_t ask_ts;
};

static_assert(sizeof(Quote) == 64, "Quote must be exactly one cache line");

template <typename Derived>
struct VenueHandlerBase {
    [[gnu::always_inline]] inline
    double adjusted_price(double price, common::Side side) noexcept {
        return static_cast<Derived*>(this)->adjusted_price_impl(price, side);
    }
};

struct LSEHandler    : VenueHandlerBase<LSEHandler>    { /* TODO */ };
struct NYSEHandler   : VenueHandlerBase<NYSEHandler>   { /* TODO */ };
struct NASDAQHandler : VenueHandlerBase<NASDAQHandler> { /* TODO */ };
struct BATSHandler   : VenueHandlerBase<BATSHandler>   { /* TODO */ };

uint64_t run_fast(const bench::GenericTick* ticks, uint32_t n);

}
