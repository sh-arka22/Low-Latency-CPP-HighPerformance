# Chapter 9 — Compile-Time Programming
> C++ High Performance (2nd Ed.) | Björn Andrist & Viktor Sehr
> Repo folder: `cpp-high-performance/09-compile-time-programming/`

---

## BFS CONCEPT MAP (start here — breadth first)

```
Compile-Time Programming
├── 1. constexpr
│   ├── constexpr variables — must be initialized with constant expression
│   ├── constexpr functions — may run at CT or RT
│   ├── consteval (C++20) — MUST run at CT (compile error if called at RT)
│   └── constinit (C++20) — static storage, CT-initialized, NOT const
│
├── 2. if constexpr  (C++17)
│   ├── Selects branch at compile time
│   ├── Discarded branch NOT instantiated → enables "generic" code on distinct types
│   └── Only valid in template context (or if condition is not value-dependent)
│
├── 3. Template Specialization
│   ├── Full specialization       — exactly one concrete set of template args
│   ├── Partial specialization   — specializes on a subset / pattern
│   └── CRTP (Curiously Recurring Template Pattern) — static polymorphism mixin
│
├── 4. SFINAE  (Substitution Failure Is Not An Error)
│   ├── std::enable_if<Cond, T> — adds/removes overload from candidate set
│   ├── std::void_t<Ts...>      — detects whether a type expression is well-formed
│   └── Detection idiom         — nonesuch + is_detected<> for capability probing
│
├── 5. Type Traits  (header <type_traits>)
│   ├── Query traits:  is_integral, is_floating_point, is_pointer, is_class …
│   ├── Modify traits: remove_const, remove_reference, add_pointer, decay …
│   ├── Composite:     conditional<B,T,F>, enable_if, common_type, result_of …
│   └── Custom traits: inherit from true_type / false_type
│
├── 6. Variadic Templates  (C++11 parameter packs, C++17 fold expressions)
│   ├── typename... / T...  — type parameter pack
│   ├── sizeof...(Ts)       — count of pack elements
│   ├── Fold expressions:   (init op ... op pack)  — replaces recursive TMP
│   └── std::tuple, std::index_sequence, std::apply
│
└── 7. C++20 Concepts  (replaces SFINAE ergonomically)
    ├── concept Foo = <constraint expression>;
    ├── requires clause on template / function
    ├── requires expression { expr; } → checks well-formedness
    ├── Abbreviated templates: auto (unconstrained) vs Concept auto
    └── Concept hierarchy / subsumption (compiler picks most-constrained overload)
```

---

## SECTION-BY-SECTION DEEP NOTES

### 1 — `constexpr`: Compile-Time Evaluation

**The core promise:** if all inputs are compile-time constants, the compiler is required to evaluate the expression at compile time and embed the result as a literal in the binary. No runtime cost whatsoever.

```cpp
constexpr int factorial(int n) {
    return (n <= 1) ? 1 : n * factorial(n - 1);
}
static_assert(factorial(10) == 3628800);  // proven at compile time
```

**Key rules (C++14 relaxed many C++11 restrictions):**
- Function body may contain local variables, loops, conditionals, multiple `return`s.
- Cannot use `goto`, `try`/`catch` (until C++20), `asm`, or undefined behavior.
- If called with non-constant arguments → runs at runtime like a normal function.
- `constexpr` implies `inline` for functions.

**`consteval` (C++20):** the function is a *compile-time-only* entity. Calling it with a runtime argument is a **compile error**. Use for things that must never leak to runtime (e.g., encryption keys baked into the binary).

```cpp
consteval int must_be_ct(int n) { return n * 2; }
int x = 5;
// must_be_ct(x);   // ERROR — x is not a constant expression
```

**`constinit` (C++20):** guarantees a variable with static or thread-local storage is *constant-initialized* (no dynamic init). Prevents the Static Initialization Order Fiasco (SIOF). The variable is **not** immutable — you can modify it after initialization.

```cpp
constinit int g_counter = 0;  // guaranteed no SIOF; can be incremented later
```

**SIOF reminder:** Two translation units, A initializes `Foo` using `Bar`, `Bar` is in another TU → undefined initialization order → crash. `constinit` forces `Bar` to be CT-initialized, eliminating the dependency.

---

### 2 — `if constexpr` (C++17)

Before C++17, writing a function that behaved differently for `int` vs `std::string` required overloads or `std::enable_if`. `if constexpr` is cleaner:

```cpp
template <typename T>
auto serialize(T val) {
    if constexpr (std::is_integral_v<T>) {
        return std::to_string(val);          // only compiled for integral T
    } else {
        return val;                           // only compiled for other T
    }
}
```

**Critical rule:** the *discarded* branch is **not instantiated**. This means it can contain code that would be ill-formed for the current type — the compiler never tries to compile it. This is fundamentally different from a regular `if` (which still compiles both branches).

**Common patterns:**
- Serialize/deserialize for primitive vs compound types.
- Network byte-order swap (noop on big-endian, `bswap` on little-endian) — checked at CT via `std::endian`.
- HFT: zero-copy path for trivially_copyable types, fallback copy for others.

---

### 3 — Template Specialization & CRTP

**Full specialization:** one concrete type, zero template parameters remaining.
```cpp
template <typename T> struct Hasher { … };
template <> struct Hasher<uint64_t> { /* fast path */ };
```

**Partial specialization (class templates only):** a pattern over template args.
```cpp
template <typename T> struct IsPointer        : std::false_type {};
template <typename T> struct IsPointer<T*>    : std::true_type  {};
```

**CRTP — Static Polymorphism:**
```cpp
template <typename Derived>
struct OrderBase {
    void submit() { static_cast<Derived*>(this)->submit_impl(); }
};

struct MarketOrder : OrderBase<MarketOrder> {
    void submit_impl() { /* ... */ }
};
```
- **No virtual table.** `submit_impl()` resolves at compile time → call can be inlined.
- Used in STL: `std::iterator`, `boost::iterator_facade`, `std::enable_shared_from_this`.
- HFT use: strategy pattern, feed handler mixins, risk-check chains — all zero virtual overhead.

---

### 4 — SFINAE

**The rule:** when the compiler substitutes template arguments into a template and the substitution produces an *invalid type or expression*, that substitution is silently dropped from the overload set — it's not a compile error.

```cpp
// enable only for arithmetic types
template <typename T, typename = std::enable_if_t<std::is_arithmetic_v<T>>>
T add(T a, T b) { return a + b; }
```

**`std::void_t<Ts...>` (C++17):** maps any well-formed list of types to `void`. Used to test whether an expression is valid:
```cpp
template <typename T, typename = void>
struct has_price : std::false_type {};

template <typename T>
struct has_price<T, std::void_t<decltype(std::declval<T>().price())>>
    : std::true_type {};
```

**Detection idiom:** generalizes `void_t` into a reusable `is_detected<Op, Args...>` — checks if `Op<Args...>` is valid without hard error.

**SFINAE pitfalls:**
- Error messages are notoriously bad ("candidate template ignored").
- Overload resolution with multiple SFINAE constraints is subtle.
- **C++20 Concepts** replace SFINAE for new code — cleaner, faster compile, better errors.

---

### 5 — Type Traits

Every type trait is a struct with a `::value` bool (or `::type` type alias). C++14 adds `_v<>` / `_t<>` helpers.

**Query categories:**
| Trait | Checks |
|---|---|
| `is_integral<T>` | bool, char, int, long, … |
| `is_floating_point<T>` | float, double, long double |
| `is_trivially_copyable<T>` | safe to `memcpy` — critical for HFT serialization |
| `is_trivially_destructible<T>` | safe to skip destructor call |
| `is_standard_layout<T>` | compatible with C structs |
| `is_same<T,U>` | exact same type |
| `is_base_of<B,D>` | B is base of D |
| `is_invocable<F,Args...>` | can call F with Args |

**Transform categories:**
| Trait | Effect |
|---|---|
| `remove_reference<T&>` → `T` | strip & or && |
| `remove_cv<const T>` → `T` | strip const/volatile |
| `decay<T>` | array→ptr, func→ptr, remove cv-ref (what auto does) |
| `add_pointer<T>` → `T*` | |
| `conditional<B,T,F>` | B?T:F at type level |

**Custom trait pattern:**
```cpp
template <typename T> struct is_price_type : std::false_type {};
template <> struct is_price_type<int32_t>  : std::true_type  {};
template <> struct is_price_type<int64_t>  : std::true_type  {};
```

---

### 6 — Variadic Templates

**Parameter packs:** `typename... Ts` or `T... args`.
- `sizeof...(Ts)` → count at compile time.
- Expand with `...` in various contexts.

**Before C++17 (recursive unpacking):**
```cpp
void print() {}  // base case
template <typename Head, typename... Tail>
void print(Head h, Tail... t) {
    std::cout << h << " ";
    print(t...);
}
```

**C++17 Fold expressions (much cleaner):**
```cpp
template <typename... Ts>
auto sum(Ts... vals) { return (... + vals); }   // left fold: ((v1+v2)+v3)…

template <typename... Ts>
void print(Ts... vals) { ((std::cout << vals << ' '), ...); }
```

Fold operators: `+`, `-`, `*`, `/`, `%`, `&`, `|`, `^`, `&&`, `||`, `,`, `<<`, `>>`.

**`std::index_sequence` pattern:**
```cpp
template <typename Tuple, std::size_t... Is>
void print_tuple_impl(const Tuple& t, std::index_sequence<Is...>) {
    ((std::cout << std::get<Is>(t) << ' '), ...);
}

template <typename... Ts>
void print_tuple(const std::tuple<Ts...>& t) {
    print_tuple_impl(t, std::index_sequence_for<Ts...>{});
}
```

**HFT use:** compile-time message factory — variadic field list, zero runtime overhead:
```cpp
template <typename... Fields>
struct Message {
    std::tuple<Fields...> fields;
    static constexpr size_t num_fields = sizeof...(Fields);
};
```

---

### 7 — C++20 Concepts

**Syntax:**
```cpp
template <typename T>
concept Numeric = std::is_arithmetic_v<T>;

template <Numeric T>          // constrained template
T add(T a, T b) { return a + b; }

auto multiply(Numeric auto a, Numeric auto b) { return a * b; }  // abbreviated
```

**`requires` expression** — tests if expressions are well-formed:
```cpp
template <typename T>
concept Priceable = requires(T t) {
    { t.price() } -> std::convertible_to<double>;
    { t.quantity() } -> std::integral;
};
```

**Concept subsumption:** if `ConceptA` requires everything `ConceptB` requires plus more, the compiler picks the more constrained overload automatically — no ambiguity.

**Why Concepts beat SFINAE:**
1. Readable error messages — "T does not satisfy Priceable" vs a wall of template instantiation noise.
2. Faster compile times (compilers can short-circuit earlier).
3. Explicit interface documentation in code.
4. Subsumption rules replace brittle overload priority tricks.

**Standard library concepts (C++20):**
`std::same_as`, `std::derived_from`, `std::convertible_to`, `std::integral`, `std::floating_point`, `std::invocable`, `std::regular`, `std::totally_ordered`, `std::ranges::range`.

---

## 25 HFT INTERVIEW QUESTIONS

### constexpr / consteval / constinit
1. **What is the difference between `constexpr` and `consteval`?**
   `constexpr` *may* evaluate at compile time; `consteval` *must*. Calling `consteval` with a runtime argument is a compile error.

2. **When does a `constexpr` function actually execute at runtime?**
   When its argument is not a constant expression (e.g., a runtime variable).

3. **What is `constinit` and what problem does it solve?**
   Guarantees constant initialization of static/thread-local variables. Prevents the Static Initialization Order Fiasco. The variable is mutable after init.

4. **What is the Static Initialization Order Fiasco?**
   Two TUs where TU-B's constructor depends on TU-A's initialized global; C++ doesn't guarantee cross-TU init order → undefined behavior. Fix: `constinit`, local static, or singleton pattern.

5. **Can `constexpr` functions contain loops?** Yes (since C++14). Also local variables, conditionals, multiple returns.

6. **How can you force a `constexpr` value to be evaluated at compile time?**
   Assign to a `constexpr` variable or pass to `static_assert`.

### if constexpr
7. **What is the key difference between `if constexpr` and a regular `if`?**
   The discarded branch of `if constexpr` is never instantiated — it can be ill-formed for the current type. A regular `if` compiles both branches.

8. **Can you use `if constexpr` outside a template?**
   Yes if the condition is a constant expression, but the main benefit (suppressing instantiation of invalid code) only applies in a template context.

### SFINAE
9. **Explain SFINAE in one sentence.**
   When template argument substitution fails (produces invalid types/expressions), the substitution is silently dropped from the overload set — it is not a compile error.

10. **What is `std::enable_if` and how does it work?**
    `enable_if<Cond, T>` defines a member type `T` when `Cond` is true; it has no member type when `Cond` is false. Using it as a default template arg adds/removes a function from the overload set.

11. **What is `std::void_t` used for?**
    Detects whether a type expression is well-formed. `void_t<decltype(t.price())>` succeeds only if `t.price()` is valid — used in trait specializations.

12. **Why is SFINAE hard to debug?**
    Error messages appear as "candidate ignored" with no indication which constraint failed. Large overload sets produce walls of diagnostics. C++20 Concepts fix this.

### Type Traits
13. **What does `std::decay<T>` do?**
    Array→pointer, function→pointer, strip top-level cv-qualifiers and references. This is what `auto` deduction does.

14. **What is `is_trivially_copyable` and why does it matter in HFT?**
    Guarantees the type can be safely `memcpy`d (no user copy constructor, no non-trivial destructor). HFT serialization can call `memcpy` instead of a copy constructor — branchless, vectorizable.

15. **Implement a trait that detects whether T has a `tick_size()` member function.**
    ```cpp
    template <typename T, typename = void>
    struct has_tick_size : std::false_type {};
    template <typename T>
    struct has_tick_size<T, std::void_t<decltype(std::declval<T>().tick_size())>>
        : std::true_type {};
    ```

### Variadic Templates
16. **What is a parameter pack?** A template parameter that accepts zero or more types (`typename... Ts`) or values (`T... args`).

17. **What is a fold expression and what is it for?**
    A C++17 feature that expands a pack with a binary operator: `(... + vals)` adds all elements. Replaces recursive TMP for many patterns.

18. **How do you iterate over a tuple's elements at compile time?**
    Via `std::index_sequence` + `std::get<I>` in a fold over an index pack.

19. **What does `sizeof...(Ts)` return?** The number of types in the pack `Ts`, at compile time.

### Concepts (C++20)
20. **Why do Concepts improve on SFINAE?**
    Better compiler errors, clearer code intent, subsumption rules for overload priority, faster compilation.

21. **What is concept subsumption?**
    If `ConceptA` requires everything `ConceptB` requires plus additional constraints, the compiler considers `ConceptA` more specialized and picks it in overload resolution — no ambiguity.

22. **Write a concept that checks T has a `.price()` returning something convertible to `double`.**
    ```cpp
    template <typename T>
    concept Priceable = requires(T t) {
        { t.price() } -> std::convertible_to<double>;
    };
    ```

23. **What is "abbreviated function template" syntax in C++20?**
    `void foo(Concept auto x)` — `auto` is implicitly a template parameter, constrained by `Concept`.

### HFT / Design
24. **Why use CRTP instead of virtual functions in a trading system?**
    CRTP resolves calls at compile time — no vtable lookup (~5 ns), no indirect branch predictor miss. The function can be inlined, enabling further optimizations (SIMD, register alloc). For ultra-low-latency code, every indirect branch costs.

25. **How would you implement a compile-time tick-size lookup table?**
    ```cpp
    struct TickEntry { int instrument_id; double tick_size; };
    constexpr TickEntry kTickTable[] = { {1, 0.01}, {2, 0.05}, … };
    constexpr double tick_size_for(int id) {
        for (auto& e : kTickTable) if (e.instrument_id == id) return e.tick_size;
        return -1.0;
    }
    static_assert(tick_size_for(1) == 0.01);  // verified at compile time
    ```
    The table lives in `.rodata` — no heap, prefetchable, O(1) from a lookup array indexed by instrument ID.

---

## KEY FORMULAS / CHEAT-SHEET

| Feature | Header | C++ std |
|---|---|---|
| `constexpr` | — | C++11 (relaxed C++14) |
| `if constexpr` | — | C++17 |
| `consteval` | — | C++20 |
| `constinit` | — | C++20 |
| `std::enable_if` | `<type_traits>` | C++11 |
| `std::void_t` | `<type_traits>` | C++17 |
| Fold expressions | — | C++17 |
| Concepts | `<concepts>` | C++20 |
| `std::is_trivially_copyable` | `<type_traits>` | C++11 |

**Compile-time evaluation hierarchy (most to least restrictive):**
`consteval` > `constexpr` > `inline` > regular function

**CRTP mixin idiom (zero overhead polymorphism):**
```
Base<Derived> → calls Derived::impl() via static_cast, inlined by compiler
```

**Concept syntax quick ref:**
```cpp
concept C = expr;                           // define
template <C T> void f(T);                  // constrain template param
void f(C auto x);                          // abbreviated template
requires (T t) { { t.foo() } -> Bar; }     // requires expression
```
