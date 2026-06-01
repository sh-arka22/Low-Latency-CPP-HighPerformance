/**
 * 01_constexpr_basics.cpp
 * Chapter 9 — Compile-Time Programming
 *
 * Covers:
 *  - constexpr variables and functions
 *  - consteval (C++20): compile-time-only guarantee
 *  - constinit (C++20): static init order fiasco prevention
 *  - static_assert to prove CT evaluation
 *  - if constexpr for zero-cost branching
 *
 * Compile:  g++ -std=c++20 -O2 -Wall -o 01_constexpr_basics 01_constexpr_basics.cpp
 */

#include <iostream>
#include <type_traits>
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// 1. Basic constexpr function — runs at CT if args are CT, RT otherwise
// ─────────────────────────────────────────────────────────────────────────────

constexpr int factorial(int n) {
    // C++14: loops and local variables allowed in constexpr
    int result = 1;
    for (int i = 2; i <= n; i++) result *= i;
    return result;
}

// Proven at compile time — no runtime cost whatsoever
static_assert(factorial(0)  == 1);
static_assert(factorial(1)  == 1);
static_assert(factorial(5)  == 120);
static_assert(factorial(10) == 3628800);

// ─────────────────────────────────────────────────────────────────────────────
// 2. constexpr variables — literals embedded in binary (.rodata)
// ─────────────────────────────────────────────────────────────────────────────

constexpr int kMaxOrders   = 1024;
constexpr double kTickSize = 0.01;
constexpr double kMaxNotional = kMaxOrders * 1000.0 * kTickSize;

static_assert(kMaxOrders == 1024);
static_assert(kMaxNotional == 1024 * 1000.0 * 0.01);

// ─────────────────────────────────────────────────────────────────────────────
// 3. consteval (C++20) — MUST evaluate at compile time
// ─────────────────────────────────────────────────────────────────────────────

consteval int compile_time_only(int n) {
    return n * n;
}

// This is fine — 7 is a constant expression
static_assert(compile_time_only(7) == 49);

// The following would NOT compile (uncomment to test):
// int x = 7;
// int y = compile_time_only(x);   // ERROR: x is not a constant expression

// ─────────────────────────────────────────────────────────────────────────────
// 4. constinit (C++20) — static-init order fiasco prevention
//    Variable is initialized at compile time, but is NOT const (can be modified)
// ─────────────────────────────────────────────────────────────────────────────

constinit int g_session_id = 42;     // initialized before any dynamic init
constinit double g_risk_limit = 1e6; // guaranteed no SIOF

// Fine to modify at runtime:
void update_risk_limit(double new_limit) { g_risk_limit = new_limit; }

// ─────────────────────────────────────────────────────────────────────────────
// 5. if constexpr — compile-time branching, discarded branch NOT instantiated
// ─────────────────────────────────────────────────────────────────────────────

template <typename T>
std::string to_string_ct(T val) {
    if constexpr (std::is_integral_v<T>) {
        return "int:" + std::to_string(val);
    } else if constexpr (std::is_floating_point_v<T>) {
        return "float:" + std::to_string(val);
    } else {
        // This branch is discarded for int/float — never compiled for those types
        return "other";
    }
}

// HFT pattern: zero-copy serialization for trivially-copyable types
template <typename T>
void serialize_to_buffer(const T& val, char* buf) {
    if constexpr (std::is_trivially_copyable_v<T>) {
        // Branchless: compile picks this for POD types, memcpy is vectorizable
        __builtin_memcpy(buf, &val, sizeof(T));
    } else {
        // Fallback: type-aware copy (only instantiated for non-trivial T)
        val.serialize(buf);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 6. constexpr + if constexpr: endian swap (HFT market data parsing)
// ─────────────────────────────────────────────────────────────────────────────

#include <bit>  // std::endian (C++20)

template <typename T>
constexpr T from_big_endian(T val) {
    static_assert(std::is_integral_v<T>, "T must be integral");
    if constexpr (std::endian::native == std::endian::little) {
        // Little-endian host: need to swap bytes
        if constexpr (sizeof(T) == 2) return __builtin_bswap16(val);
        if constexpr (sizeof(T) == 4) return __builtin_bswap32(val);
        if constexpr (sizeof(T) == 8) return __builtin_bswap64(val);
    }
    return val;  // big-endian host: no-op
}

static_assert(sizeof(from_big_endian<uint32_t>(0x01020304)) == 4);

// ─────────────────────────────────────────────────────────────────────────────
// 7. Prove CT vs RT evaluation
// ─────────────────────────────────────────────────────────────────────────────

void demonstrate_ct_vs_rt() {
    // CT: result embedded as literal in binary
    constexpr int ct_result = factorial(12);
    std::cout << "CT factorial(12) = " << ct_result << "\n";  // 479001600

    // RT: value from stdin — cannot evaluate at CT
    int n;
    std::cout << "Enter n for factorial: ";
    std::cin >> n;
    int rt_result = factorial(n);  // same function, runs at runtime
    std::cout << "RT factorial(" << n << ") = " << rt_result << "\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// 8. constexpr class — useful for compile-time config objects
// ─────────────────────────────────────────────────────────────────────────────

struct VenueConfig {
    int    max_orders;
    double tick_size;
    double max_notional;

    constexpr VenueConfig(int max, double tick)
        : max_orders(max)
        , tick_size(tick)
        , max_notional(max * tick * 1000.0)
    {}
};

constexpr VenueConfig kNasdaqConfig { 2048, 0.01 };
constexpr VenueConfig kCMEConfig    { 512,  0.25 };

static_assert(kNasdaqConfig.max_orders   == 2048);
static_assert(kCMEConfig.tick_size       == 0.25);

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────
int main() {
    std::cout << "=== constexpr Basics ===\n\n";

    // Static asserts already verified at compile time — these are just runtime prints
    std::cout << "factorial(10) = " << factorial(10) << " (proven CT via static_assert)\n";
    std::cout << "kMaxOrders    = " << kMaxOrders    << "\n";
    std::cout << "kMaxNotional  = " << kMaxNotional  << "\n\n";

    std::cout << "to_string_ct(42)   = " << to_string_ct(42)    << "\n";
    std::cout << "to_string_ct(3.14) = " << to_string_ct(3.14)  << "\n";
    std::cout << "to_string_ct('x')  = " << to_string_ct('x')   << "\n\n";

    std::cout << "NasdaqConfig: max=" << kNasdaqConfig.max_orders
              << " tick=" << kNasdaqConfig.tick_size << "\n";
    std::cout << "CMEConfig:    max=" << kCMEConfig.max_orders
              << " tick=" << kCMEConfig.tick_size << "\n\n";

    std::cout << "g_session_id  = " << g_session_id  << " (constinit, can be modified)\n";
    std::cout << "g_risk_limit  = " << g_risk_limit  << "\n";
    update_risk_limit(5e6);
    std::cout << "g_risk_limit  = " << g_risk_limit  << " (after update)\n";

    return 0;
}
