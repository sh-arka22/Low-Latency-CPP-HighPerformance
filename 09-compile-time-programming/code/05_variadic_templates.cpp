/**
 * 05_variadic_templates.cpp
 * Chapter 9 — Compile-Time Programming
 *
 * Covers:
 *  - Parameter packs (typename... Ts, T... args)
 *  - sizeof...(Ts): count at compile time
 *  - Pre-C++17 recursive unpacking
 *  - C++17 fold expressions (left fold, right fold, binary fold)
 *  - std::index_sequence for tuple traversal
 *  - TypeList: compile-time type sequence
 *  - HFT: variadic message layout, static field iteration
 *
 * Compile:  g++ -std=c++20 -O2 -Wall -o 05_variadic_templates 05_variadic_templates.cpp
 */

#include <iostream>
#include <tuple>
#include <type_traits>
#include <string>
#include <cstdint>
#include <utility>

// ─────────────────────────────────────────────────────────────────────────────
// 1. Basic Variadic Template — sizeof... and pack expansion
// ─────────────────────────────────────────────────────────────────────────────

template <typename... Ts>
constexpr size_t type_count() { return sizeof...(Ts); }

static_assert(type_count<>() == 0);
static_assert(type_count<int>() == 1);
static_assert(type_count<int, double, char>() == 3);

// ─────────────────────────────────────────────────────────────────────────────
// 2. Pre-C++17: Recursive unpacking (understand this to read old code)
// ─────────────────────────────────────────────────────────────────────────────

// Base case: empty pack
void print_recursive() {
    std::cout << "\n";
}

// Recursive case: head + tail
template <typename Head, typename... Tail>
void print_recursive(const Head& h, const Tail&... t) {
    std::cout << h;
    if constexpr (sizeof...(t) > 0) std::cout << ", ";
    print_recursive(t...);
}

// ─────────────────────────────────────────────────────────────────────────────
// 3. C++17 Fold Expressions — cleaner, no recursion needed
// ─────────────────────────────────────────────────────────────────────────────

// Left fold: ((v1 + v2) + v3) + ...
template <typename... Ts>
constexpr auto sum(Ts... vals) { return (... + vals); }

// Right fold: v1 + (v2 + (v3 + ...))
template <typename... Ts>
constexpr auto sum_right(Ts... vals) { return (vals + ...); }

// Unary fold over comma: execute f for each element
template <typename... Ts>
void print_all(Ts... vals) {
    ((std::cout << vals << ' '), ...);
    std::cout << "\n";
}

// Binary fold with init: (0 + ... + vals)
template <typename... Ts>
auto sum_with_init(Ts... vals) { return (0 + ... + vals); }

// All-of and any-of patterns
template <typename... Ts>
constexpr bool all_positive(Ts... vals) { return ((vals > 0) && ...); }

template <typename... Ts>
constexpr bool any_negative(Ts... vals) { return ((vals < 0) || ...); }

static_assert(sum(1, 2, 3, 4, 5) == 15);
static_assert(all_positive(1, 2, 3));
static_assert(!all_positive(1, -1, 3));
static_assert(any_negative(-1, 2, 3));
static_assert(!any_negative(1, 2, 3));

// ─────────────────────────────────────────────────────────────────────────────
// 4. std::index_sequence — compile-time integer sequences for tuple access
// ─────────────────────────────────────────────────────────────────────────────

// Helper: print every element of a tuple
template <typename Tuple, std::size_t... Is>
void print_tuple_impl(const Tuple& t, std::index_sequence<Is...>) {
    // Fold over comma: calls cout for each index
    ((std::cout << std::get<Is>(t) << (Is+1 < sizeof...(Is) ? ", " : "")), ...);
    std::cout << "\n";
}

template <typename... Ts>
void print_tuple(const std::tuple<Ts...>& t) {
    print_tuple_impl(t, std::index_sequence_for<Ts...>{});
}

// Apply a function to each tuple element
template <typename Tuple, typename F, std::size_t... Is>
void for_each_impl(Tuple& t, F&& f, std::index_sequence<Is...>) {
    (f(std::get<Is>(t)), ...);
}

template <typename... Ts, typename F>
void for_each(std::tuple<Ts...>& t, F&& f) {
    for_each_impl(t, std::forward<F>(f),
        std::index_sequence_for<Ts...>{});
}

// ─────────────────────────────────────────────────────────────────────────────
// 5. TypeList — compile-time list of types
// ─────────────────────────────────────────────────────────────────────────────

template <typename... Ts> struct TypeList {
    static constexpr size_t size = sizeof...(Ts);
};

// Length
template <typename List> struct Length;
template <typename... Ts>
struct Length<TypeList<Ts...>> : std::integral_constant<size_t, sizeof...(Ts)> {};

// Head
template <typename List> struct Head;
template <typename H, typename... Ts>
struct Head<TypeList<H, Ts...>> { using type = H; };

// Tail
template <typename List> struct Tail;
template <typename H, typename... Ts>
struct Tail<TypeList<H, Ts...>> { using type = TypeList<Ts...>; };

// Contains (fold expression in a trait)
template <typename T, typename List> struct Contains;
template <typename T, typename... Ts>
struct Contains<T, TypeList<Ts...>>
    : std::bool_constant<(std::is_same_v<T, Ts> || ...)> {};

using OrderTypes = TypeList<int32_t, int64_t, double, char>;

static_assert(Length<OrderTypes>::value == 4);
static_assert(std::is_same_v<Head<OrderTypes>::type, int32_t>);
static_assert(Contains<double, OrderTypes>::value);
static_assert(!Contains<float, OrderTypes>::value);

// ─────────────────────────────────────────────────────────────────────────────
// 6. Variadic function — perfect forwarding constructor
// ─────────────────────────────────────────────────────────────────────────────

template <typename... Args>
std::tuple<Args...> make_order_fields(Args&&... args) {
    return std::make_tuple(std::forward<Args>(args)...);
}

// ─────────────────────────────────────────────────────────────────────────────
// 7. HFT: Variadic Message struct with static field count
// ─────────────────────────────────────────────────────────────────────────────

template <typename... Fields>
struct Message {
    using FieldTuple = std::tuple<Fields...>;
    static constexpr size_t num_fields = sizeof...(Fields);

    FieldTuple fields;

    explicit Message(Fields... f) : fields(std::forward<Fields>(f)...) {}

    // Serialize all fields to buffer via for_each
    size_t serialize(char* buf) const {
        size_t offset = 0;
        for_each_impl(const_cast<FieldTuple&>(fields),
            [&](const auto& field) {
                using F = std::decay_t<decltype(field)>;
                if constexpr (std::is_trivially_copyable_v<F>) {
                    __builtin_memcpy(buf + offset, &field, sizeof(F));
                    offset += sizeof(F);
                }
            },
            std::index_sequence_for<Fields...>{});
        return offset;
    }
};

using QuoteMsg = Message<uint64_t, int32_t, int32_t, int32_t, int32_t>;
//                       instr_id  bid_px   ask_px   bid_qty  ask_qty

static_assert(QuoteMsg::num_fields == 5);

// ─────────────────────────────────────────────────────────────────────────────
// 8. Variadic max — returns largest of N values at compile time
// ─────────────────────────────────────────────────────────────────────────────

template <typename T>
constexpr T ct_max(T a) { return a; }

template <typename T, typename... Ts>
constexpr T ct_max(T a, T b, Ts... rest) {
    return ct_max(a > b ? a : b, rest...);
}

static_assert(ct_max(1, 5, 3, 2, 4) == 5);
static_assert(ct_max(10, 20) == 20);

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────
int main() {
    std::cout << "=== Variadic Templates ===\n\n";

    // Recursive print
    std::cout << "print_recursive: ";
    print_recursive(1, 2.5, "hello", 'x');

    // Fold expressions
    std::cout << "\n--- Fold Expressions ---\n";
    std::cout << "sum(1..5)     = " << sum(1, 2, 3, 4, 5) << "\n";
    std::cout << "sum_with_init = " << sum_with_init(10, 20, 30) << "\n";
    std::cout << "print_all: ";
    print_all(1, 2.5, "three", 4);

    std::cout << "all_positive(1,2,3)   = " << all_positive(1, 2, 3) << "\n";
    std::cout << "any_negative(-1,2,3)  = " << any_negative(-1, 2, 3) << "\n";

    // Tuple traversal via index_sequence
    std::cout << "\n--- Tuple Traversal ---\n";
    auto t = std::make_tuple(42, 3.14, std::string("AAPL"), true);
    std::cout << "Tuple: ";
    print_tuple(t);

    std::cout << "for_each double: ";
    for_each(t, [](auto& x) {
        if constexpr (std::is_same_v<std::decay_t<decltype(x)>, double>) {
            std::cout << "double=" << x << " ";
        }
    });
    std::cout << "\n";

    // TypeList
    std::cout << "\n--- TypeList ---\n";
    std::cout << "OrderTypes::size = " << OrderTypes::size << "\n";
    std::cout << "Contains<double> = " << Contains<double, OrderTypes>::value << "\n";
    std::cout << "Contains<float>  = " << Contains<float,  OrderTypes>::value << "\n";

    // HFT Message
    std::cout << "\n--- Variadic HFT Message ---\n";
    QuoteMsg quote{1001ULL, 15025, 15026, 100, 200};
    std::cout << "QuoteMsg fields:   " << QuoteMsg::num_fields << "\n";
    std::cout << "tuple field bid:   " << std::get<1>(quote.fields) << "\n";

    char buf[64];
    size_t bytes = quote.serialize(buf);
    std::cout << "Serialized bytes:  " << bytes << "\n";  // 4*4 + 8 = 24

    // ct_max
    std::cout << "\n--- compile-time max ---\n";
    constexpr int m = ct_max(3, 1, 4, 1, 5, 9, 2, 6);
    std::cout << "ct_max(3,1,4,1,5,9,2,6) = " << m << "\n";

    return 0;
}
