/**
 * 03_sfinae_enable_if.cpp
 * Chapter 9 — Compile-Time Programming
 *
 * Covers:
 *  - SFINAE mechanics: how substitution failure becomes a silent overload removal
 *  - std::enable_if: three syntactic forms
 *  - std::void_t: expression validity detection
 *  - Detection idiom: nonesuch + is_detected
 *  - std::declval: create objects in unevaluated contexts
 *
 * Compile:  g++ -std=c++20 -O2 -Wall -o 03_sfinae_enable_if 03_sfinae_enable_if.cpp
 */

#include <iostream>
#include <type_traits>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// 1. Basic SFINAE — the compiler silently drops invalid overloads
// ─────────────────────────────────────────────────────────────────────────────

// Overload A: only for integral types
template <typename T>
typename std::enable_if<std::is_integral_v<T>, std::string>::type
describe(T) { return "integral"; }

// Overload B: only for floating-point types
template <typename T>
typename std::enable_if<std::is_floating_point_v<T>, std::string>::type
describe(T) { return "floating_point"; }

// For anything else: a fallback is only needed if you want to handle it
// Without it, calling describe with an unsupported type → "no matching function"

// ─────────────────────────────────────────────────────────────────────────────
// 2. std::enable_if — three syntactic forms
// ─────────────────────────────────────────────────────────────────────────────

// Form 1: Default template parameter (most common for class constraints)
template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
struct IntBox {
    T value;
    explicit IntBox(T v) : value(v) {}
};
// IntBox<double> box{3.14};   // would be a compile error

// Form 2: Return type (clean for function overloads)
template <typename T>
std::enable_if_t<std::is_arithmetic_v<T>, T>
safe_abs(T x) { return x < 0 ? -x : x; }

// Form 3: Extra pointer parameter (avoids return-type issues with constructors)
template <typename T, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
void print_precise(T val) {
    std::cout << std::fixed << val << "\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// 3. std::void_t — detect if a type expression is well-formed
// ─────────────────────────────────────────────────────────────────────────────

// Detect if T has a .price() method
template <typename T, typename = void>
struct has_price : std::false_type {};

template <typename T>
struct has_price<T, std::void_t<decltype(std::declval<T>().price())>>
    : std::true_type {};

// Detect if T has a .submit() method
template <typename T, typename = void>
struct has_submit : std::false_type {};

template <typename T>
struct has_submit<T, std::void_t<decltype(std::declval<T>().submit())>>
    : std::true_type {};

// Detect if T has .price() returning something convertible to double
template <typename T, typename = void>
struct has_double_price : std::false_type {};

template <typename T>
struct has_double_price<
    T,
    std::void_t<decltype(static_cast<double>(std::declval<T>().price()))>
> : std::true_type {};

// Test types
struct SpotOrder {
    double price() const { return 100.0; }
    void   submit() {}
};

struct FuturesOrder {
    int price() const { return 100; }   // returns int, not double
};

struct MarketOrder {
    // no price(), no submit()
};

static_assert( has_price<SpotOrder>::value);
static_assert( has_price<FuturesOrder>::value);
static_assert(!has_price<MarketOrder>::value);

static_assert( has_submit<SpotOrder>::value);
static_assert(!has_submit<FuturesOrder>::value);

static_assert( has_double_price<SpotOrder>::value);
static_assert( has_double_price<FuturesOrder>::value);  // int→double conversion ok

// ─────────────────────────────────────────────────────────────────────────────
// 4. std::declval — declare an object in unevaluated context
// ─────────────────────────────────────────────────────────────────────────────

// Useful when T has no default constructor
struct NonDefaultConstructible {
    NonDefaultConstructible(int) {}
    double price() const { return 42.0; }
};

// declval<T>() returns T&& without constructing anything
using PriceType = decltype(std::declval<NonDefaultConstructible>().price());
static_assert(std::is_same_v<PriceType, double>);

// ─────────────────────────────────────────────────────────────────────────────
// 5. Detection Idiom — generalized capability probe
// ─────────────────────────────────────────────────────────────────────────────

// The "nonesuch" type — cannot be constructed, used as a sentinel
struct nonesuch {
    ~nonesuch() = delete;
    nonesuch() = delete;
    nonesuch(nonesuch const&) = delete;
    void operator=(nonesuch const&) = delete;
};

// Detector machinery
template <typename Default, typename AlwaysVoid,
          template <typename...> class Op, typename... Args>
struct detector {
    using value_t = std::false_type;
    using type    = Default;
};

template <typename Default,
          template <typename...> class Op, typename... Args>
struct detector<Default, std::void_t<Op<Args...>>, Op, Args...> {
    using value_t = std::true_type;
    using type    = Op<Args...>;
};

// Public interface
template <template <typename...> class Op, typename... Args>
using is_detected = typename detector<nonesuch, void, Op, Args...>::value_t;

template <template <typename...> class Op, typename... Args>
using detected_t = typename detector<nonesuch, void, Op, Args...>::type;

// Define "operations" (type aliases for what we want to detect)
template <typename T> using price_op   = decltype(std::declval<T>().price());
template <typename T> using submit_op  = decltype(std::declval<T>().submit());
template <typename T> using qty_op     = decltype(std::declval<T>().quantity());

// Use the detection idiom
static_assert( is_detected<price_op,  SpotOrder>::value);
static_assert(!is_detected<price_op,  MarketOrder>::value);
static_assert( is_detected<submit_op, SpotOrder>::value);
static_assert(!is_detected<qty_op,    SpotOrder>::value);

// Get the detected type (or nonesuch if not detected)
using SpotPriceType = detected_t<price_op, SpotOrder>;
static_assert(std::is_same_v<SpotPriceType, double>);

using MarketPriceType = detected_t<price_op, MarketOrder>;
static_assert(std::is_same_v<MarketPriceType, nonesuch>);  // not detected

// ─────────────────────────────────────────────────────────────────────────────
// 6. HFT Pattern: dispatch to fast/slow path based on detected capabilities
// ─────────────────────────────────────────────────────────────────────────────

template <typename T>
void route_order(const T& order) {
    if constexpr (is_detected<submit_op, T>::value) {
        // Fast path: type has a submit() method — use it directly
        std::cout << "Direct submit path\n";
        const_cast<T&>(order).submit();
    } else if constexpr (is_detected<price_op, T>::value) {
        // Fallback: type has price but no submit — use generic gateway
        std::cout << "Gateway route for order with price\n";
    } else {
        // Market order: no price, no submit — special handling
        std::cout << "Market order path (no price, no submit)\n";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────
int main() {
    std::cout << "=== SFINAE & enable_if ===\n\n";

    // Basic SFINAE
    std::cout << "describe(42)   = " << describe(42)   << "\n";
    std::cout << "describe(3.14) = " << describe(3.14) << "\n";

    // enable_if forms
    std::cout << "\n--- safe_abs ---\n";
    std::cout << "safe_abs(-7)    = " << safe_abs(-7)    << "\n";
    std::cout << "safe_abs(-3.14) = " << safe_abs(-3.14) << "\n";

    // void_t trait detection
    std::cout << "\n--- Trait Detection (void_t) ---\n";
    std::cout << "SpotOrder has price:    " << has_price<SpotOrder>::value    << "\n";
    std::cout << "MarketOrder has price:  " << has_price<MarketOrder>::value  << "\n";
    std::cout << "SpotOrder has submit:   " << has_submit<SpotOrder>::value   << "\n";
    std::cout << "FuturesOrder has submit:" << has_submit<FuturesOrder>::value<< "\n";

    // Detection idiom
    std::cout << "\n--- Detection Idiom ---\n";
    std::cout << "is_detected<price_op, SpotOrder>:   "
              << is_detected<price_op, SpotOrder>::value << "\n";
    std::cout << "is_detected<price_op, MarketOrder>: "
              << is_detected<price_op, MarketOrder>::value << "\n";

    // Order routing
    std::cout << "\n--- Order Routing ---\n";
    SpotOrder    spot;
    FuturesOrder futures;
    MarketOrder  market;
    route_order(spot);
    route_order(futures);
    route_order(market);

    return 0;
}
