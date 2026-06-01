/**
 * 02_template_specialization.cpp
 * Chapter 9 — Compile-Time Programming
 *
 * Covers:
 *  - Full template specialization
 *  - Partial template specialization
 *  - Tag dispatch pattern (zero-overhead type selection)
 *  - CRTP (Curiously Recurring Template Pattern) — static polymorphism
 *  - CRTP mixin for multiple compile-time policies
 *
 * Compile:  g++ -std=c++20 -O2 -Wall -o 02_template_specialization 02_template_specialization.cpp
 */

#include <iostream>
#include <cstring>
#include <cstdint>
#include <type_traits>
#include <chrono>

// ─────────────────────────────────────────────────────────────────────────────
// 1. Full Template Specialization
// ─────────────────────────────────────────────────────────────────────────────

// Primary template: generic serializer
template <typename T>
struct Serializer {
    static void serialize(const T& val, char* buf) {
        // Generic fallback: call T's member serialize
        val.serialize(buf);
    }
    static constexpr size_t size() { return sizeof(T); }
};

// Full specialization for uint32_t — raw copy, no method call
template <>
struct Serializer<uint32_t> {
    static void serialize(uint32_t val, char* buf) {
        __builtin_memcpy(buf, &val, 4);
    }
    static constexpr size_t size() { return 4; }
};

// Full specialization for double
template <>
struct Serializer<double> {
    static void serialize(double val, char* buf) {
        __builtin_memcpy(buf, &val, 8);
    }
    static constexpr size_t size() { return 8; }
};

// ─────────────────────────────────────────────────────────────────────────────
// 2. Partial Template Specialization
//    (Only allowed for class templates, not function templates)
// ─────────────────────────────────────────────────────────────────────────────

// Primary: generic
template <typename T>
struct IsPointerToConst { static constexpr bool value = false; };

// Partial: any T const*
template <typename T>
struct IsPointerToConst<const T*> { static constexpr bool value = true; };

static_assert(!IsPointerToConst<int*>::value);
static_assert( IsPointerToConst<const int*>::value);
static_assert( IsPointerToConst<const double*>::value);

// HFT: partial spec to detect price types
template <typename T> struct IsPriceType   : std::false_type {};
template <>           struct IsPriceType<int32_t>  : std::true_type {};
template <>           struct IsPriceType<int64_t>  : std::true_type {};

static_assert( IsPriceType<int32_t>::value);
static_assert(!IsPriceType<double>::value);

// ─────────────────────────────────────────────────────────────────────────────
// 3. Tag Dispatch — zero-overhead compile-time type selection
//    Better than SFINAE for many cases — readable and fast
// ─────────────────────────────────────────────────────────────────────────────

struct IntegralTag   {};
struct FloatingTag   {};
struct OtherTag      {};

template <typename T>
using SelectTag = std::conditional_t<
    std::is_integral_v<T>,
    IntegralTag,
    std::conditional_t<std::is_floating_point_v<T>, FloatingTag, OtherTag>
>;

// Implementation overloads selected by tag — no runtime overhead
template <typename T>
void print_type_impl(T val, IntegralTag) {
    std::cout << "INT: " << val << "\n";
}
template <typename T>
void print_type_impl(T val, FloatingTag) {
    std::cout << "FLOAT: " << val << "\n";
}
template <typename T>
void print_type_impl(T val, OtherTag) {
    std::cout << "OTHER\n";
}

// Public interface: tag is a zero-size temporary, eliminated by compiler
template <typename T>
void print_type(T val) {
    print_type_impl(val, SelectTag<T>{});
}

// ─────────────────────────────────────────────────────────────────────────────
// 4. CRTP — Static Polymorphism (zero virtual overhead)
// ─────────────────────────────────────────────────────────────────────────────

// CRTP base: defines interface, dispatches to Derived
template <typename Derived>
struct FeedHandlerBase {
    // Public interface
    void on_quote(uint64_t instr_id, double bid, double ask) {
        // Static cast — resolved at compile time, call can be inlined
        static_cast<Derived*>(this)->handle_quote(instr_id, bid, ask);
    }

    void on_trade(uint64_t instr_id, double price, int qty) {
        static_cast<Derived*>(this)->handle_trade(instr_id, price, qty);
    }
};

// Concrete handler A — low-latency path
struct FastFeedHandler : FeedHandlerBase<FastFeedHandler> {
    void handle_quote(uint64_t id, double bid, double ask) {
        // No cout on hot path — just store
        (void)id; (void)bid; (void)ask;
    }
    void handle_trade(uint64_t id, double price, int qty) {
        (void)id; (void)price; (void)qty;
    }
};

// Concrete handler B — debug path with logging
struct DebugFeedHandler : FeedHandlerBase<DebugFeedHandler> {
    void handle_quote(uint64_t id, double bid, double ask) {
        std::cout << "QUOTE id=" << id << " bid=" << bid << " ask=" << ask << "\n";
    }
    void handle_trade(uint64_t id, double price, int qty) {
        std::cout << "TRADE id=" << id << " price=" << price << " qty=" << qty << "\n";
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// 5. CRTP Mixin — multiple policies composed at compile time
// ─────────────────────────────────────────────────────────────────────────────

// Mixin 1: latency logging
template <typename Derived>
struct LatencyMixin {
    void record_send_time() {
        last_send_ns_ = std::chrono::high_resolution_clock::now()
                        .time_since_epoch().count();
    }
    int64_t latency_ns() {
        return std::chrono::high_resolution_clock::now()
               .time_since_epoch().count() - last_send_ns_;
    }
private:
    int64_t last_send_ns_ = 0;
};

// Mixin 2: risk guard
template <typename Derived>
struct RiskMixin {
    bool risk_check(double notional) const {
        return notional < static_cast<const Derived*>(this)->max_notional_;
    }
};

// Composite trader inheriting both mixins — all calls inlined, no virtual
struct AlgoTrader
    : LatencyMixin<AlgoTrader>
    , RiskMixin<AlgoTrader>
{
    static constexpr double max_notional_ = 1e6;

    void submit_order(double notional) {
        if (!risk_check(notional)) {
            std::cout << "REJECTED: notional=" << notional << "\n";
            return;
        }
        record_send_time();
        // … send
        std::cout << "SENT: notional=" << notional
                  << " latency=" << latency_ns() << " ns\n";
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// 6. Benchmark: CRTP vs virtual dispatch
// ─────────────────────────────────────────────────────────────────────────────

struct VirtualBase {
    virtual void process(int) = 0;
    virtual ~VirtualBase() = default;
};

struct VirtualDerived : VirtualBase {
    void process(int x) override { (void)x; }
};

template <typename Derived>
struct CRTPBase {
    void process(int x) { static_cast<Derived*>(this)->process_impl(x); }
};

struct CRTPDerived : CRTPBase<CRTPDerived> {
    void process_impl(int x) { (void)x; }
};

void benchmark_dispatch() {
    constexpr int N = 50'000'000;
    auto now = std::chrono::high_resolution_clock::now;

    // Virtual
    VirtualBase* vb = new VirtualDerived();
    auto t0 = now();
    for (int i = 0; i < N; i++) vb->process(i);
    auto t1 = now();
    double virtual_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1-t0).count() / double(N);
    delete vb;

    // CRTP
    CRTPDerived cd;
    auto t2 = now();
    for (int i = 0; i < N; i++) cd.process(i);
    auto t3 = now();
    double crtp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t3-t2).count() / double(N);

    std::cout << "\n=== Dispatch Benchmark (" << N/1'000'000 << "M calls) ===\n";
    std::cout << "Virtual: " << virtual_ns << " ns/call\n";
    std::cout << "CRTP:    " << crtp_ns   << " ns/call\n";
    std::cout << "Speedup: " << virtual_ns / crtp_ns << "×\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────
int main() {
    std::cout << "=== Template Specialization & CRTP ===\n\n";

    // Serializer
    char buf[16];
    Serializer<uint32_t>::serialize(0xDEADBEEF, buf);
    std::cout << "Serialized uint32_t size: " << Serializer<uint32_t>::size() << " bytes\n";

    // Tag dispatch
    std::cout << "\n--- Tag Dispatch ---\n";
    print_type(42);
    print_type(3.14);
    print_type('x');    // integral (char is integral)

    // CRTP feed handler
    std::cout << "\n--- CRTP Feed Handlers ---\n";
    DebugFeedHandler debug;
    debug.on_quote(1001, 150.25, 150.26);
    debug.on_trade(1001, 150.25, 100);

    // CRTP mixin
    std::cout << "\n--- CRTP Mixin (AlgoTrader) ---\n";
    AlgoTrader trader;
    trader.submit_order(500000.0);   // under limit
    trader.submit_order(2000000.0);  // over limit

    // Benchmark
    benchmark_dispatch();

    return 0;
}
