// 05_soa_vs_aos.cpp
// ---------------------------------------------------------------------
// Chapter 4 — Array of Structures vs Structure of Arrays
//
// Build:  g++ -std=c++20 -O3 -march=native -Wall -Wextra 05_soa_vs_aos.cpp -o 05_soa
//
// Demonstrates the cache-line waste in AoS when the hot loop touches one
// field, vs SoA which packs that field densely. SoA should win 2-10x.
//
// To dig deeper: perf stat -e cache-misses,cache-references ./05_soa
// ---------------------------------------------------------------------

#include <chrono>
#include <cstdint>
#include <iostream>
#include <vector>

// ---------- AoS layout ----------
struct OrderAoS {
    double        price;
    std::int32_t  qty;
    std::uint64_t id;
    bool          is_buy;
};
// sizeof(OrderAoS) on x86-64 == 24 (with padding)

// ---------- SoA layout ----------
struct OrdersSoA {
    std::vector<double>        prices;
    std::vector<std::int32_t>  qtys;
    std::vector<std::uint64_t> ids;
    std::vector<std::uint8_t>  is_buy;   // bool packed into byte
};

static double sum_prices_aos(const std::vector<OrderAoS>& v) {
    double s = 0.0;
    for (const auto& o : v) s += o.price;  // reads 8 of 24 bytes per element
    return s;
}

static double sum_prices_soa(const OrdersSoA& s) {
    double sum = 0.0;
    for (double p : s.prices) sum += p;     // reads 8 of 8 bytes per element
    return sum;
}

int main() {
    std::cout << "=== AoS vs SoA: sum of prices over 50M orders ===\n";
    std::cout << "sizeof(OrderAoS) = " << sizeof(OrderAoS) << " bytes (with padding)\n\n";

    constexpr std::size_t N = 50'000'000;
    std::vector<OrderAoS> aos(N);
    OrdersSoA             soa;
    soa.prices.resize(N);
    soa.qtys.resize(N);
    soa.ids.resize(N);
    soa.is_buy.resize(N);

    for (std::size_t i = 0; i < N; ++i) {
        double p = 100.0 + double(i % 1000) * 0.01;
        aos[i] = { p, 10, i, (i & 1) == 0 };
        soa.prices[i] = p;
        soa.qtys[i]   = 10;
        soa.ids[i]    = i;
        soa.is_buy[i] = (i & 1) == 0;
    }

    // Warm up the cache once
    [[maybe_unused]] auto warmup1 = sum_prices_aos(aos);
    [[maybe_unused]] auto warmup2 = sum_prices_soa(soa);

    auto t0 = std::chrono::steady_clock::now();
    double s_aos = sum_prices_aos(aos);
    auto t1 = std::chrono::steady_clock::now();
    double s_soa = sum_prices_soa(soa);
    auto t2 = std::chrono::steady_clock::now();

    auto ms_aos = std::chrono::duration<double, std::milli>(t1 - t0).count();
    auto ms_soa = std::chrono::duration<double, std::milli>(t2 - t1).count();

    std::cout << "AoS sum: " << s_aos << "   time: " << ms_aos << " ms\n";
    std::cout << "SoA sum: " << s_soa << "   time: " << ms_soa << " ms\n";
    std::cout << "Speedup SoA/AoS = " << (ms_aos / ms_soa) << "x\n";
    std::cout << "Expected: 2-10x depending on machine + compiler.\n";
    return 0;
}
