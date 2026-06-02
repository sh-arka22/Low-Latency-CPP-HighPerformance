# Section 6 Deep Dive — C++20 Concepts

> **Source**: [06_concepts.cpp](file:///Users/arkaj/Desktop/Low-Latency-CPP/mini_quote_engine/cpp-high-performance/09-compile-time-programming/code/06_concepts.cpp) (264 lines)
> **Build**: `g++ -std=c++20 -O2 -Wall -o 06_concepts 06_concepts.cpp`

---

## 📋 Program Output

```
=== C++20 Concepts ===

add_v1(1,2)   = 3
add_v4(1.0,2.5) = 3.5

--- Concept Checks ---
Numeric<int>:       1
Numeric<double>:    1
SignedInteger<int>: 1
SignedInteger<uint>:0

--- HFT Order Concept Hierarchy ---
Instrument<MarketOrder>: 1
Order<MarketOrder>:      1
PricedOrder<MarketOrder>:0
PricedOrder<LimitOrder>: 1

--- Concept Subsumption ---
route(Order): generic order routing
route(PricedOrder): limit order routing, price=150.25

--- Standard Library Concepts ---
next_tick(100, 1)       = 101
midpoint(150.24, 150.26)= 150.25
call_safely(mult, 6, 7) = 42

--- RiskEngine<ConcreteLimitOrder> ---
risk.check(limit):        1
risk.check_price(limit):  1
```

---

## 🔬 Section-by-Section Breakdown

In Section 3, we looked at SFINAE and `std::void_t`. It was a powerful but ugly hack to force the compiler to check if a type had certain capabilities. 

**Concepts (introduced in C++20) are the official language feature designed to replace SFINAE.** They allow you to put strict rules on template parameters using clean, readable syntax.

### §1. The Four Syntaxes (Lines 40–53)

A Concept is just a boolean rule. Once defined, you can apply it to a template in four different ways:

```cpp
// 1. The classic "requires" clause (cleanest for complex rules)
template <typename T> requires Numeric<T>
T add(T a, T b) { return a + b; }

// 2. The prefix syntax (cleanest for single parameters)
template <Numeric T>
T add(T a, T b) { return a + b; }

// 3. The trailing requires clause (useful for class methods)
template <typename T>
T add(T a, T b) requires Numeric<T> { return a + b; }

// 4. The "Concept Auto" syntax (shorthand, no template bracket needed!)
Numeric auto add(Numeric auto a, Numeric auto b) { return a + b; }
```

> [!TIP]
> Use **Syntax 2 (`requires`)** when you have multiple concepts or complex logic (`requires Numeric<T> && !std::is_pointer_v<T>`). Use **Syntax 4 (`auto`)** for simple, one-off generic functions.

---

### §2. The `requires` Expression (Lines 59–84)

This is the replacement for `std::void_t` and `declval`. A `requires` expression allows you to write "dummy code" to see if it compiles. If it compiles, the concept is `true`.

```cpp
template <typename T>
concept Serializable = requires(T t, char* buf, size_t n) {
    // Rule 1: t.serialize(buf, n) must compile, AND return something convertible to size_t
    { t.serialize(buf, n) } -> std::convertible_to<size_t>;
    
    // Rule 2: t.size_bytes() must compile, AND return exactly size_t
    { t.size_bytes() }      -> std::same_as<size_t>;
};
```
No `declval`, no `SFINAE` failure hacks, no `nonesuch` sentinels. Just clean, declarative requirements.

---

### §3. HFT Domain Hierarchy (Lines 92–118)

In object-oriented programming (OOP), you build hierarchies using inheritance (Base Class → Derived Class).
In modern generic programming, **you build hierarchies using Concepts**.

```cpp
// 1. Base level concept
template <typename T>
concept Instrument = requires(const T t) {
    { t.symbol() } -> std::convertible_to<std::string_view>;
};

// 2. Derived concept (Order IS-A Instrument + extra stuff)
template <typename T>
concept Order = Instrument<T> && requires(const T t) {
    { t.side() } -> std::same_as<Side>;
};

// 3. Leaf concept (PricedOrder IS-A Order + extra stuff)
template <typename T>
concept PricedOrder = Order<T> && requires(const T t) {
    { t.limit_price() } -> std::same_as<double>;
};
```

This acts exactly like an inheritance tree, but the concrete classes (`ConcreteLimitOrder`, `ConcreteMarketOrder`) **do not inherit from any base classes**. They are just flat, simple structs (PODs) that happen to satisfy the rules. This gives you the strict interface enforcement of OOP with the zero-overhead, fully-inlined performance of templates!

---

### §4. Concept Subsumption (Overload Resolution) (Lines 159–168)

What happens if you have two template functions, and an object matches BOTH of them? 

```cpp
template <Order O>
void route(const O& o) { std::cout << "generic routing\n"; }

template <PricedOrder O>
void route(const O& o) { std::cout << "limit routing\n"; }
```

In the old SFINAE days, this would cause a devastating "Ambiguous Overload" compiler error. You would have had to manually add `!PricedOrder<O>` to the first template to fix it.

**C++20 Concepts fix this automatically via Subsumption.**
The compiler analyzes the definitions of the concepts:
- `Order` requires A and B.
- `PricedOrder` requires A, B, and C.

The compiler mathematically proves that `PricedOrder` is **more strictly constrained** than `Order` (it *subsumes* it). So, if you pass a `ConcreteLimitOrder` (which satisfies both), the compiler automatically picks the `PricedOrder` overload without ambiguity!

```cpp
ConcreteMarketOrder market;
ConcreteLimitOrder  limit;

route(market); // Only satisfies Order -> prints "generic routing"
route(limit);  // Satisfies both -> Subsumption picks PricedOrder -> prints "limit routing"
```

---

### §5. Concept-Constrained Risk Engine (Lines 195–215)

The most powerful place to use concepts is constraining your classes.

```cpp
template <Order O>
class RiskEngine {
public:
    bool check(const O& order) const {
        // We can safely call .quantity() and .is_valid() here!
        // If O didn't have them, the class wouldn't have even instantiated.
        if (!order.is_valid()) return false;
        return true;
    }
    
    // We can even constrain specific methods within the class!
    template <PricedOrder P>
    bool check_price(const P& order) const { ... }
};
```

Because `RiskEngine` requires `Order O`, you get **IDE autocomplete** inside the class for `order.quantity()`. You couldn't do that with old templates, because the compiler didn't know what `O` was until it was instantiated.

---

## 🧠 Key Takeaways

1. **Better Error Messages:** The main selling point of Concepts. If you pass an invalid type to a Concept-constrained template, the compiler says exactly what was missing (e.g., *"Constraint not satisfied: `limit_price()` is not defined on `ConcreteMarketOrder`"*). SFINAE used to just dump 100 lines of "no matching function found" template diarrhea.
2. **Compile-Time "Duck Typing" Perfected:** You define what an object should *look* like and *do*, rather than what it should *inherit* from.
3. **Zero Overhead:** Concepts are purely a compile-time mechanism. They exist only to guide template instantiation and overload resolution. The resulting binary is perfectly inlined direct calls, exactly as we proved in Sections 2 and 3.

---

**That concludes the deep dive into Chapter 9 — Compile-Time Programming!**

We went from simple `constexpr` calculations, through the horrors of CRTP and SFINAE, into Variadic Metaprogramming, and finally arrived at the clean, modern syntax of C++20 Concepts. 

Are there any specific areas of compile-time programming you'd like to explore further, or are we ready to move on to the next major topic in your HFT codebase?
