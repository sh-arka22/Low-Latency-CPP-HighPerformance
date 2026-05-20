#pragma once

#include <cstdint>

namespace common {

constexpr uint32_t NUM_TICKS = 10'000'000;
constexpr uint8_t  NUM_VENUES = 4;
constexpr double   MIN_PRICE = 0.01;
constexpr double   MAX_PRICE = 999'999.0;

enum class VenueID : uint8_t {
    LSE    = 0,
    NYSE   = 1,
    NASDAQ = 2,
    BATS   = 3,
};

enum class Side : uint8_t {
    BUY  = 0,
    SELL = 1,
};

[[gnu::always_inline]] inline
uint64_t rdtsc() noexcept {
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return (uint64_t(hi) << 32) | lo;
}

}
