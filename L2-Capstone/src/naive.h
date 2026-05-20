#pragma once

#include "../include/common.h"
#include "../bench/gen_data.h"
#include <cstdint>
#include <functional>
#include <memory>

namespace naive {

struct Tick {

};

struct Quote {
    double   best_bid;
    double   best_ask;
    uint64_t bid_ts;
    uint64_t ask_ts;
};

class VenueHandlerBase {
public:
    virtual ~VenueHandlerBase() = default;
    virtual double adjusted_price(double price, common::Side side) = 0;
    virtual bool is_active() const = 0;
};

class LSEHandler    : public VenueHandlerBase { /* TODO */ };
class NYSEHandler   : public VenueHandlerBase { /* TODO */ };
class NASDAQHandler : public VenueHandlerBase { /* TODO */ };
class BATSHandler   : public VenueHandlerBase { /* TODO */ };

uint64_t run_naive(const bench::GenericTick* ticks, uint32_t n);

}
