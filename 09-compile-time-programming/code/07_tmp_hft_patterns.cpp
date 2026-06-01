/**
 * 07_tmp_hft_patterns.cpp
 * Chapter 9 — Compile-Time Programming
 *
 * HFT production patterns using compile-time programming:
 *  1. Compile-time tick-size lookup table (constexpr array, perfect hash)
 *  2. FixedString — compile-time symbol storage (no heap)
 *  3. Policy-based OrderRouter (zero virtual overhead)
 *  4. Compile-time message field layout validation
 *  5. Tag-based order type dispatch — zero branches at runtime
 *  6. constexpr FNV-1a hash for symbol interning
 *
 * Compile:  g++ -std=c++20 -O2 -Wall -o 07_tmp_hft_patterns 07_tmp_hft_patterns.cpp
 */

#include <iostream>
#include <array>
#include <string_view>
#include <cstdint>
#include <type_traits>
#include <cstring>
#include <chrono>
#include <cassert>

// ─────────────────────────────────────────────────────────────────────────────
// 1. Compile-Time Tick-Size Lookup Table
//    Lives in .rodata — no heap, prefetchable, zero dynamic init
// ─────────────────────────────────────────────────────────────────────────────

struct TickEntry {
    uint32_t instrument_id;
    double   tick_size;
    int32_t  lot_size;
    double   max_notional;
};

constexpr TickEntry kTickTable[] = {
    { 1001, 0.01,  100,  1e7 },   // AAPL  (equity)
    { 1002, 0.05,   10,  5e6 },   // BRK.B (equity)
    { 2001, 0.25,    1,  2e8 },   // ES    (S&P500 futures)
    { 2002, 0.50,    1,  1e8 },   // NQ    (Nasdaq futures)
    { 3001, 0.01,    1,  5e6 },   // EURUSD (FX)
    { 3002, 0.001,   1,  3e6 },   // GBPUSD (FX)
};

constexpr size_t kTickTableSize = sizeof(kTickTable) / sizeof(kTickTable[0]);

// Linear scan — fine for small tables; compiler may unroll entirely
constexpr const TickEntry* find_tick_entry(uint32_t id) {
    for (const auto& e : kTickTable)
        if (e.instrument_id == id) return &e;
    return nullptr;
}

// Compile-time verification
static_assert(find_tick_entry(1001)->tick_size   == 0.01);
static_assert(find_tick_entry(1001)->lot_size    == 100);
static_assert(find_tick_entry(2001)->tick_size   == 0.25);
static_assert(find_tick_entry(9999)              == nullptr);

// ─────────────────────────────────────────────────────────────────────────────
// 2. constexpr FNV-1a Hash — symbol interning at compile time
// ─────────────────────────────────────────────────────────────────────────────

constexpr uint64_t fnv1a_hash(std::string_view s) {
    uint64_t hash = 14695981039346656037ULL;
    for (char c : s) {
        hash ^= static_cast<uint64_t>(c);
        hash *= 1099511628211ULL;
    }
    return hash;
}

static_assert(fnv1a_hash("AAPL") != fnv1a_hash("MSFT"));
static_assert(fnv1a_hash("AAPL") == fnv1a_hash("AAPL"));  // deterministic

// ─────────────────────────────────────────────────────────────────────────────
// 3. FixedString — compile-time string (C++20 NTTP class type)
//    No heap, sizeof == N, fits in registers for small symbols
// ─────────────────────────────────────────────────────────────────────────────

template <size_t N>
struct FixedString {
    char data[N] = {};

    // Construct from string literal — copy each character at CT
    constexpr FixedString(const char (&s)[N]) {
        for (size_t i = 0; i < N; i++) data[i] = s[i];
    }

    constexpr bool operator==(const FixedString&) const = default;
    constexpr std::string_view view() const { return {data, N-1}; }  // exclude \0
    constexpr size_t size()          const { return N - 1; }
    constexpr uint64_t hash()        const { return fnv1a_hash(view()); }
};

// C++20: class types as Non-Type Template Parameters
template <FixedString Symbol>
struct InstrumentTag {
    static constexpr auto name = Symbol;
    static constexpr uint64_t hash = Symbol.hash();
};

using AAPL_Tag = InstrumentTag<"AAPL">;
using ES_Tag   = InstrumentTag<"ES">;

static_assert(AAPL_Tag::name.view() == "AAPL");
static_assert(ES_Tag::name.size()   == 2);
static_assert(AAPL_Tag::hash != ES_Tag::hash);

// ─────────────────────────────────────────────────────────────────────────────
// 4. Policy-Based OrderRouter — zero virtual overhead
//    Different combinations produce different binaries, all maximally optimized
// ─────────────────────────────────────────────────────────────────────────────

// Policy: logging
struct NullLogger {
    static constexpr void log(const char*) {}  // completely dead-code-eliminated
};

struct StderrLogger {
    static void log(const char* msg) { std::cerr << "[LOG] " << msg << "\n"; }
};

// Policy: risk
struct NoRisk {
    static constexpr bool check(double) { return true; }  // eliminated: always true
};

struct BasicRisk {
    double max_notional;
    constexpr BasicRisk(double m) : max_notional(m) {}
    bool check(double notional) const { return notional < max_notional; }
};

// Policy: execution venue
struct DirectExec {
    static void send(uint32_t id, double price, int qty) {
        (void)id; (void)price; (void)qty;
        // Directly write to NIC ring buffer
    }
};

struct SimExec {
    static void send(uint32_t id, double price, int qty) {
        std::cout << "SIM SEND: id=" << id
                  << " price=" << price << " qty=" << qty << "\n";
    }
};

template <
    typename Logger  = NullLogger,
    typename Risk    = NoRisk,
    typename Exec    = DirectExec
>
class OrderRouter {
public:
    // Constructor takes the risk policy (may have state)
    explicit OrderRouter(Risk risk = Risk{}) : risk_(std::move(risk)) {}

    void send(uint32_t instr_id, double price, int qty) {
        double notional = price * qty;

        // Risk check — if NoRisk, compiler eliminates entire branch
        if (!risk_.check(notional)) {
            Logger::log("REJECTED: notional limit exceeded");
            return;
        }

        Logger::log("ORDER SENT");
        Exec::send(instr_id, price, qty);
    }

private:
    [[no_unique_address]] Risk risk_;  // zero-size if Risk is stateless
};

// Prod: no logging, no risk check (done upstream), direct NIC write
using ProdRouter = OrderRouter<NullLogger, NoRisk, DirectExec>;

// Staging: logging + risk check + sim execution
using StagingRouter = OrderRouter<StderrLogger, BasicRisk, SimExec>;

// ─────────────────────────────────────────────────────────────────────────────
// 5. Compile-Time Message Layout Validation
//    Ensures message structs have no padding that would corrupt wire format
// ─────────────────────────────────────────────────────────────────────────────

struct __attribute__((packed)) QuoteMessage {
    uint64_t seq_num;    // 8
    uint32_t instr_id;   // 4
    int32_t  bid_price;  // 4 (integer ticks)
    int32_t  ask_price;  // 4
    int32_t  bid_qty;    // 4
    int32_t  ask_qty;    // 4
    uint64_t timestamp;  // 8
};                       // total = 36

static_assert(sizeof(QuoteMessage) == 36,
    "QuoteMessage has unexpected padding — wire format corrupted");
static_assert(std::is_trivially_copyable_v<QuoteMessage>,
    "QuoteMessage must be trivially copyable for memcpy serialization");
static_assert(std::is_standard_layout_v<QuoteMessage>,
    "QuoteMessage must be standard layout for C interop");

// ─────────────────────────────────────────────────────────────────────────────
// 6. Tag-based Order Type Dispatch — compile-time branch selection
//    No virtual, no dynamic_cast, no type() member — pure compile-time dispatch
// ─────────────────────────────────────────────────────────────────────────────

struct MarketOrderTag {};
struct LimitOrderTag  {};
struct StopOrderTag   {};

template <typename Tag>
struct OrderDispatcher;

template <>
struct OrderDispatcher<MarketOrderTag> {
    static void execute(double, int qty) {
        std::cout << "Market: execute " << qty << " at best\n";
    }
};

template <>
struct OrderDispatcher<LimitOrderTag> {
    static void execute(double price, int qty) {
        std::cout << "Limit: post " << qty << " @ " << price << "\n";
    }
};

template <>
struct OrderDispatcher<StopOrderTag> {
    static void execute(double price, int qty) {
        std::cout << "Stop: trigger at " << price << ", qty=" << qty << "\n";
    }
};

template <typename Tag>
void dispatch_order(double price, int qty) {
    OrderDispatcher<Tag>::execute(price, qty);  // resolved at compile time, inlined
}

// ─────────────────────────────────────────────────────────────────────────────
// 7. Benchmark: constexpr lookup vs runtime switch
// ─────────────────────────────────────────────────────────────────────────────

void benchmark_lookup() {
    constexpr int N = 10'000'000;
    auto now = std::chrono::high_resolution_clock::now;

    // RT lookup: switch statement
    volatile double result_switch = 0;
    auto t0 = now();
    for (int i = 0; i < N; i++) {
        uint32_t id = (i % 6 == 0) ? 1001 :
                      (i % 6 == 1) ? 1002 :
                      (i % 6 == 2) ? 2001 :
                      (i % 6 == 3) ? 2002 :
                      (i % 6 == 4) ? 3001 : 3002;
        const auto* e = find_tick_entry(id);
        if (e) result_switch += e->tick_size;
    }
    auto t1 = now();
    double switch_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1-t0).count() / double(N);

    std::cout << "\n=== Benchmark (" << N/1'000'000 << "M lookups) ===\n";
    std::cout << "constexpr table scan: " << switch_ns << " ns/lookup (result=" << result_switch << ")\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────
int main() {
    std::cout << "=== HFT Compile-Time Patterns ===\n\n";

    // Tick table
    std::cout << "--- Tick Table ---\n";
    for (uint32_t id : {1001u, 2001u, 3001u, 9999u}) {
        const auto* e = find_tick_entry(id);
        if (e) {
            std::cout << "id=" << id << " tick=" << e->tick_size
                      << " lot=" << e->lot_size << "\n";
        } else {
            std::cout << "id=" << id << " NOT FOUND\n";
        }
    }

    // FNV hash
    std::cout << "\n--- FNV-1a Hash ---\n";
    constexpr uint64_t h_aapl = fnv1a_hash("AAPL");
    constexpr uint64_t h_msft = fnv1a_hash("MSFT");
    std::cout << "hash(AAPL) = " << h_aapl << "\n";
    std::cout << "hash(MSFT) = " << h_msft << "\n";

    // FixedString / NTTP
    std::cout << "\n--- FixedString NTTP ---\n";
    std::cout << "AAPL_Tag::name = " << AAPL_Tag::name.view() << "\n";
    std::cout << "ES_Tag::name   = " << ES_Tag::name.view()   << "\n";
    std::cout << "Same hash?      " << (AAPL_Tag::hash == ES_Tag::hash) << "\n";

    // Policy-based router
    std::cout << "\n--- Policy-Based OrderRouter ---\n";
    ProdRouter prod;
    prod.send(1001, 150.25, 100);  // emits zero log/risk code — straight to Exec::send

    StagingRouter staging{BasicRisk{5e6}};
    staging.send(1001, 150.25, 100);                   // under limit
    staging.send(1001, 150.25, 100'000);               // over limit

    // Tag-based dispatch
    std::cout << "\n--- Tag-Based Order Dispatch ---\n";
    dispatch_order<MarketOrderTag>(0.0, 100);
    dispatch_order<LimitOrderTag>(150.25, 200);
    dispatch_order<StopOrderTag>(148.00, 300);

    // Message layout
    std::cout << "\n--- Wire Message Validation ---\n";
    std::cout << "sizeof(QuoteMessage) = " << sizeof(QuoteMessage) << " bytes\n";
    std::cout << "trivially_copyable:   " << std::is_trivially_copyable_v<QuoteMessage> << "\n";

    // Benchmark
    benchmark_lookup();

    return 0;
}
