/**
 * 04_type_traits.cpp
 * Chapter 9 — Compile-Time Programming
 *
 * Covers:
 *  - Query traits: is_integral, is_trivially_copyable, is_same, etc.
 *  - Transform traits: remove_reference, decay, conditional
 *  - Custom type traits using true_type / false_type
 *  - if constexpr + type traits for zero-overhead generic code
 *  - std::integral_constant as a compile-time value carrier
 *
 * Compile:  g++ -std=c++20 -O2 -Wall -o 04_type_traits 04_type_traits.cpp
 */

#include <iostream>
#include <type_traits>
#include <array>
#include <cstring>
#include <cstdint>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// 1. Query Traits — compile-time type interrogation
// ─────────────────────────────────────────────────────────────────────────────

void demo_query_traits() {
    std::cout << "--- Query Traits ---\n";

    // Arithmetic category
    static_assert(std::is_integral_v<int>);
    static_assert(std::is_integral_v<uint64_t>);
    static_assert(!std::is_integral_v<double>);
    static_assert(std::is_floating_point_v<double>);
    static_assert(std::is_arithmetic_v<int>);       // integral || floating_point

    // Object properties
    struct Pod { int x; double y; };
    struct WithDtor { ~WithDtor() { } };

    static_assert(std::is_trivially_copyable_v<Pod>);         // safe to memcpy
    static_assert(!std::is_trivially_copyable_v<std::string>);
    static_assert(std::is_trivially_destructible_v<Pod>);
    static_assert(!std::is_trivially_destructible_v<WithDtor>);
    static_assert(std::is_standard_layout_v<Pod>);            // C-compatible layout

    // Relationships
    static_assert(std::is_same_v<int, int>);
    static_assert(!std::is_same_v<int, long>);

    struct Base {};
    struct Derived : Base {};
    static_assert(std::is_base_of_v<Base, Derived>);
    static_assert(!std::is_base_of_v<Derived, Base>);

    std::cout << "All static_assert checks passed.\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// 2. Transform Traits — type manipulation at compile time
// ─────────────────────────────────────────────────────────────────────────────

void demo_transform_traits() {
    std::cout << "\n--- Transform Traits ---\n";

    // remove_reference
    static_assert(std::is_same_v<std::remove_reference_t<int&>,  int>);
    static_assert(std::is_same_v<std::remove_reference_t<int&&>, int>);
    static_assert(std::is_same_v<std::remove_reference_t<int>,   int>);

    // remove_cv (const/volatile)
    static_assert(std::is_same_v<std::remove_cv_t<const int>, int>);
    static_assert(std::is_same_v<std::remove_cv_t<volatile int>, int>);
    static_assert(std::is_same_v<std::remove_const_t<const double>, double>);

    // decay — models "pass by value" deduction rules
    // 1. removes ref
    // 2. removes top-level cv
    // 3. array → pointer
    // 4. function → function pointer
    static_assert(std::is_same_v<std::decay_t<const int&>, int>);
    static_assert(std::is_same_v<std::decay_t<int[5]>,     int*>);
    static_assert(std::is_same_v<std::decay_t<int(int)>,   int(*)(int)>);

    // conditional: B ? T : F at the type level
    using TypeA = std::conditional_t<true,  int, double>;   // int
    using TypeB = std::conditional_t<false, int, double>;   // double
    static_assert(std::is_same_v<TypeA, int>);
    static_assert(std::is_same_v<TypeB, double>);

    // add_pointer / add_const
    static_assert(std::is_same_v<std::add_pointer_t<int>, int*>);
    static_assert(std::is_same_v<std::add_const_t<int>, const int>);

    std::cout << "All transform trait checks passed.\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// 3. Custom Type Traits — HFT domain types
// ─────────────────────────────────────────────────────────────────────────────

// Mark types that represent prices (integers scaled by tick size)
template <typename T> struct is_price_integer    : std::false_type {};
template <>           struct is_price_integer<int32_t>  : std::true_type {};
template <>           struct is_price_integer<int64_t>  : std::true_type {};
template <typename T> inline constexpr bool is_price_integer_v = is_price_integer<T>::value;

static_assert( is_price_integer_v<int32_t>);
static_assert( is_price_integer_v<int64_t>);
static_assert(!is_price_integer_v<double>);
static_assert(!is_price_integer_v<float>);

// Detect if a type is a "sequence type" (array or vector-like)
// Using partial specialization on arrays
template <typename T>       struct is_fixed_array : std::false_type {};
template <typename T, size_t N>
struct is_fixed_array<T[N]> : std::true_type { static constexpr size_t size = N; };
template <typename T, size_t N>
struct is_fixed_array<std::array<T, N>> : std::true_type { static constexpr size_t size = N; };

// Composite trait: type is safe for zero-copy network serialization
template <typename T>
inline constexpr bool is_wire_safe_v =
    std::is_trivially_copyable_v<T>   &&
    std::is_standard_layout_v<T>      &&
    !std::is_pointer_v<T>;

struct WireMessage {
    uint64_t seq_num;
    int32_t  price;
    int32_t  quantity;
};
static_assert(is_wire_safe_v<WireMessage>);
static_assert(!is_wire_safe_v<std::string>);

// ─────────────────────────────────────────────────────────────────────────────
// 4. std::integral_constant — carrying values in types
// ─────────────────────────────────────────────────────────────────────────────

using MaxOrders  = std::integral_constant<int, 1024>;
using TickSize   = std::integral_constant<int, 100>;   // 100 = $0.01 in integer ticks

static_assert(MaxOrders::value == 1024);
static_assert(TickSize::value  == 100);

// integral_constant works as a tag — zero-size, no runtime cost
template <int N>
void process_n_orders(std::integral_constant<int, N>) {
    std::cout << "Processing " << N << " orders (CT constant)\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// 5. Generic serialize using type traits — zero-overhead path selection
// ─────────────────────────────────────────────────────────────────────────────

template <typename T>
size_t safe_serialize(const T& val, char* buf) {
    if constexpr (is_wire_safe_v<T>) {
        // memcpy path: no constructor calls, vectorizable, ~1 ns
        std::memcpy(buf, &val, sizeof(T));
        return sizeof(T);
    } else if constexpr (std::is_same_v<T, std::string>) {
        // String path: length-prefixed
        uint32_t len = static_cast<uint32_t>(val.size());
        std::memcpy(buf, &len, 4);
        std::memcpy(buf + 4, val.data(), len);
        return 4 + len;
    } else {
        static_assert(sizeof(T) == 0, "No serialization strategy for this type");
        return 0;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 6. type_identity — prevent unwanted type deduction (C++20)
// ─────────────────────────────────────────────────────────────────────────────

// Forces the caller to explicitly specify T for the second argument
template <typename T>
void set_price(T& obj, std::type_identity_t<T> price) {
    (void)obj; (void)price;
}

// ─────────────────────────────────────────────────────────────────────────────
// 7. Result type deduction with common_type
// ─────────────────────────────────────────────────────────────────────────────

template <typename T, typename U>
auto add_prices(T a, U b) {
    using R = std::common_type_t<T, U>;  // picks the "wider" type
    return static_cast<R>(a) + static_cast<R>(b);
}

static_assert(std::is_same_v<std::common_type_t<int, double>, double>);
static_assert(std::is_same_v<std::common_type_t<int, long>,   long>);

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────
int main() {
    std::cout << "=== Type Traits ===\n\n";

    demo_query_traits();
    demo_transform_traits();

    std::cout << "\n--- Custom Traits ---\n";
    std::cout << "is_price_integer<int32_t>: " << is_price_integer_v<int32_t> << "\n";
    std::cout << "is_price_integer<double>:  " << is_price_integer_v<double>  << "\n";
    std::cout << "is_wire_safe<WireMessage>: " << is_wire_safe_v<WireMessage> << "\n";
    std::cout << "is_wire_safe<std::string>: " << is_wire_safe_v<std::string> << "\n";

    std::cout << "\n--- integral_constant ---\n";
    process_n_orders(MaxOrders{});
    process_n_orders(TickSize{});

    std::cout << "\n--- safe_serialize ---\n";
    WireMessage msg{100, 15025, 500};
    char buf[64];
    size_t bytes = safe_serialize(msg, buf);
    std::cout << "Serialized WireMessage: " << bytes << " bytes\n";

    std::string sym = "AAPL";
    bytes = safe_serialize(sym, buf);
    std::cout << "Serialized std::string: " << bytes << " bytes (4 len + " << sym.size() << " chars)\n";

    std::cout << "\n--- add_prices ---\n";
    std::cout << "add_prices(100, 200LL) = " << add_prices(100, 200LL) << "\n";
    std::cout << "add_prices(100, 0.5)   = " << add_prices(100, 0.5)   << "\n";

    return 0;
}
