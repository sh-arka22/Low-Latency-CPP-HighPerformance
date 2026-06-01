/**
 * 06_concepts.cpp
 * Chapter 9 — Compile-Time Programming
 *
 * Covers:
 *  - concept definition syntax
 *  - requires clause and requires expression
 *  - Constrained template parameters
 *  - Abbreviated function templates (Concept auto)
 *  - Concept hierarchy and subsumption
 *  - Standard library concepts (C++20 <concepts>)
 *  - HFT: domain concept hierarchy for order types
 *
 * Compile:  g++ -std=c++20 -O2 -Wall -o 06_concepts 06_concepts.cpp
 */

#include <iostream>
#include <functional>
#include <concepts>
#include <string_view>
#include <cstdint>
#include <type_traits>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// 1. Basic Concept Definition
// ─────────────────────────────────────────────────────────────────────────────

// A concept is just a named constraint expression that evaluates to bool
template <typename T>
concept Numeric = std::is_arithmetic_v<T>;

template <typename T>
concept SignedInteger = std::is_integral_v<T> && std::is_signed_v<T>;

// ─────────────────────────────────────────────────────────────────────────────
// 2. Constraining template parameters — four equivalent syntaxes
// ─────────────────────────────────────────────────────────────────────────────

// Syntax 1: concept name before parameter
template <Numeric T>
T add_v1(T a, T b) { return a + b; }

// Syntax 2: requires clause after template parameter list
template <typename T> requires Numeric<T>
T add_v2(T a, T b) { return a + b; }

// Syntax 3: trailing requires clause (most flexible, can use function params)
template <typename T>
T add_v3(T a, T b) requires Numeric<T> { return a + b; }

// Syntax 4: abbreviated template (auto with concept name)
Numeric auto add_v4(Numeric auto a, Numeric auto b) { return a + b; }

// ─────────────────────────────────────────────────────────────────────────────
// 3. requires Expression — test well-formedness of expressions
// ─────────────────────────────────────────────────────────────────────────────

// Simple: check expressions are valid
template <typename T>
concept Addable = requires(T a, T b) {
    a + b;          // expression must be well-formed
};

// With return type constraint
template <typename T>
concept Printable = requires(T t) {
    { std::cout << t };  // must be printable via <<
};

// Compound: multiple requirements
template <typename T>
concept Serializable = requires(T t, char* buf, size_t n) {
    { t.serialize(buf, n) } -> std::convertible_to<size_t>;
    { t.size_bytes() }      -> std::same_as<size_t>;
};

// Nested requires inside requires expression
template <typename T>
concept BidirectionalIterable = requires(T t) {
    t.begin();
    t.end();
    requires std::bidirectional_iterator<decltype(t.begin())>;
};

// ─────────────────────────────────────────────────────────────────────────────
// 4. HFT Domain Concept Hierarchy
// ─────────────────────────────────────────────────────────────────────────────

enum class Side : uint8_t { Buy, Sell };

// Level 1: Any tradeable instrument
template <typename T>
concept Instrument = requires(const T t) {
    { t.symbol()    } -> std::convertible_to<std::string_view>;
    { t.tick_size() } -> std::same_as<double>;
    { t.lot_size()  } -> std::same_as<int>;
};

// Level 2: Instrument + order fields
template <typename T>
concept Order = Instrument<T> && requires(const T t) {
    { t.side()     } -> std::same_as<Side>;
    { t.quantity() } -> std::integral;
    { t.is_valid() } -> std::same_as<bool>;
};

// Level 3: Order with a price (limit/stop)
template <typename T>
concept PricedOrder = Order<T> && requires(const T t) {
    { t.limit_price() } -> std::same_as<double>;
};

// Level 4: Order with a time-in-force
template <typename T>
concept TIFOrder = Order<T> && requires(const T t) {
    { t.time_in_force() } -> std::convertible_to<int>;
};

// ─────────────────────────────────────────────────────────────────────────────
// 5. Concrete Order Types satisfying the hierarchy
// ─────────────────────────────────────────────────────────────────────────────

struct BaseInstrument {
    std::string_view symbol()    const { return "AAPL"; }
    double           tick_size() const { return 0.01; }
    int              lot_size()  const { return 100; }
};

struct ConcreteMarketOrder : BaseInstrument {
    Side side()      const { return Side::Buy; }
    int  quantity()  const { return 500; }
    bool is_valid()  const { return quantity() > 0; }
};

struct ConcreteLimitOrder : BaseInstrument {
    Side   side()        const { return Side::Buy; }
    int    quantity()    const { return 200; }
    bool   is_valid()    const { return quantity() > 0 && limit_price() > 0; }
    double limit_price() const { return 150.25; }
    int    time_in_force() const { return 0; }  // DAY
};

// Verify concepts are satisfied
static_assert(Instrument<ConcreteMarketOrder>);
static_assert(Order<ConcreteMarketOrder>);
static_assert(!PricedOrder<ConcreteMarketOrder>);  // no limit_price()

static_assert(Instrument<ConcreteLimitOrder>);
static_assert(Order<ConcreteLimitOrder>);
static_assert(PricedOrder<ConcreteLimitOrder>);
static_assert(TIFOrder<ConcreteLimitOrder>);

// ─────────────────────────────────────────────────────────────────────────────
// 6. Overload resolution via concept subsumption
//    Compiler picks the MOST CONSTRAINED overload automatically
// ─────────────────────────────────────────────────────────────────────────────

template <Order O>
void route(const O& o) {
    std::cout << "route(Order): generic order routing\n";
}

template <PricedOrder O>   // more constrained than Order → preferred when applicable
void route(const O& o) {
    std::cout << "route(PricedOrder): limit order routing, price="
              << o.limit_price() << "\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// 7. Standard Library Concepts (C++20 <concepts>)
// ─────────────────────────────────────────────────────────────────────────────

// std::same_as, std::derived_from, std::convertible_to
// std::integral, std::floating_point, std::arithmetic
// std::invocable, std::regular, std::totally_ordered

template <std::integral T>
T next_tick(T price, T tick) { return price + tick; }

template <std::floating_point T>
T midpoint(T bid, T ask) { return (bid + ask) / T{2}; }

// std::invocable — callable concept
template <typename F, typename... Args>
    requires std::invocable<F, Args...>
auto call_safely(F&& f, Args&&... args) {
    return std::invoke(std::forward<F>(f), std::forward<Args>(args)...);
}

// ─────────────────────────────────────────────────────────────────────────────
// 8. Concept-constrained risk engine
// ─────────────────────────────────────────────────────────────────────────────

template <Order O>
class RiskEngine {
public:
    bool check(const O& order) const {
        // Compiler knows O has .quantity() and .is_valid() — no SFINAE noise
        if (!order.is_valid())      return false;
        if (order.quantity() > max_qty_) return false;
        return true;
    }

    // Only enabled for PricedOrder (concept constraint in member template)
    template <PricedOrder P>
        requires std::same_as<P, O>
    bool check_price(const P& order) const {
        return order.limit_price() > 0 && order.limit_price() < max_price_;
    }

private:
    int    max_qty_   = 1000;
    double max_price_ = 10000.0;
};

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────
int main() {
    std::cout << "=== C++20 Concepts ===\n\n";

    // Basic constrained templates
    std::cout << "add_v1(1,2)   = " << add_v1(1, 2)      << "\n";
    std::cout << "add_v4(1.0,2.5) = " << add_v4(1.0, 2.5) << "\n";

    // Concept checks
    std::cout << "\n--- Concept Checks ---\n";
    std::cout << "Numeric<int>:       " << Numeric<int>    << "\n";
    std::cout << "Numeric<double>:    " << Numeric<double> << "\n";
    std::cout << "SignedInteger<int>: " << SignedInteger<int> << "\n";
    std::cout << "SignedInteger<uint>:" << SignedInteger<unsigned int> << "\n";

    // HFT domain concepts
    std::cout << "\n--- HFT Order Concept Hierarchy ---\n";
    std::cout << "Instrument<MarketOrder>: " << Instrument<ConcreteMarketOrder> << "\n";
    std::cout << "Order<MarketOrder>:      " << Order<ConcreteMarketOrder>      << "\n";
    std::cout << "PricedOrder<MarketOrder>:" << PricedOrder<ConcreteMarketOrder> << "\n";
    std::cout << "PricedOrder<LimitOrder>: " << PricedOrder<ConcreteLimitOrder>  << "\n";

    // Subsumption — compiler picks most constrained overload
    std::cout << "\n--- Concept Subsumption ---\n";
    ConcreteMarketOrder market;
    ConcreteLimitOrder  limit;
    route(market);   // picks Order overload
    route(limit);    // picks PricedOrder overload (more constrained)

    // Standard concepts
    std::cout << "\n--- Standard Library Concepts ---\n";
    std::cout << "next_tick(100, 1)       = " << next_tick(100, 1)           << "\n";
    std::cout << "midpoint(150.24, 150.26)= " << midpoint(150.24, 150.26)   << "\n";

    auto mult = [](int a, int b) { return a * b; };
    std::cout << "call_safely(mult, 6, 7) = " << call_safely(mult, 6, 7)   << "\n";

    // RiskEngine
    std::cout << "\n--- RiskEngine<ConcreteLimitOrder> ---\n";
    RiskEngine<ConcreteLimitOrder> risk;
    std::cout << "risk.check(limit):        " << risk.check(limit)        << "\n";
    std::cout << "risk.check_price(limit):  " << risk.check_price(limit)  << "\n";

    return 0;
}
