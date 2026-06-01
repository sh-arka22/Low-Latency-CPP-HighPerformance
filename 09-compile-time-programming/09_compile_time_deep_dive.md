# Chapter 9 — Compile-Time Programming: HFT Deep Dive
> C++ High Performance (2nd Ed.) | Björn Andrist & Viktor Sehr
> Companion doc to NOTES.md — goes beyond the book into production HFT patterns

---

## 1. WHY COMPILE-TIME PROGRAMMING IS CRITICAL FOR HFT

Every CPU cycle on the hot path costs ~0.3 ns at 3 GHz. A virtual function call costs 5–10 ns (vtable lookup + branch mispredict). An unnecessary branch costs 1–15 ns (mispredict). A heap allocation costs 50–500 ns.

Compile-time programming eliminates entire categories of runtime cost:
- **Virtual dispatch** → replaced by CRTP (inlined, 0 ns overhead)
- **Runtime type checks** → replaced by `if constexpr` / Concepts (eliminated entirely from binary)
- **Runtime lookup tables** → replaced by `constexpr` arrays (data in `.rodata`, pure cache-line reads)
- **Dynamic configuration** → replaced by policy templates (different binaries for different configs, max optimization per config)

The HFT firm compiles a different binary for each trading venue, each feed type, each risk profile. The compiler does all the "if" work at build time.

---

## 2. `constexpr` INTERNALS: WHAT THE COMPILER ACTUALLY DOES

### Constant Expression Evaluation
The compiler has a built-in interpreter for `constexpr` functions. It literally executes the function at compile time, tracking all values. This is why complex `constexpr` code (recursive, looping) can explode compile times.

**Compiler constant-folds at multiple levels:**
1. Literal constants: `3 + 4` → `7` always
2. `constexpr` variables: `constexpr int N = factorial(10);` → `3628800` embedded in `.rodata`
3. `consteval` functions: fully evaluated, disappear from AST
4. Template NTTPs: `std::array<int, N>` → `N` resolved at template instantiation

### What Goes Into the Binary
```
constexpr int TICK_TABLE[3] = { 10, 5, 25 };
```
This produces:
```asm
TICK_TABLE:
    .long 10, 5, 25     ; in .rodata section — no initialization code at all
```
A runtime `int arr[] = {…}` would be in `.data` with possible dynamic initialization.

### `constexpr` Limitations (C++17 and C++20 progressively relax them)
| Feature | C++11 | C++14 | C++17 | C++20 |
|---|---|---|---|---|
| Local variables | ❌ | ✅ | ✅ | ✅ |
| Loops | ❌ | ✅ | ✅ | ✅ |
| Multiple returns | ❌ | ✅ | ✅ | ✅ |
| `try/catch` | ❌ | ❌ | ❌ | ✅ |
| `new/delete` | ❌ | ❌ | ❌ | ✅ (CT heap) |
| `virtual` calls | ❌ | ❌ | ❌ | ✅ |
| `union` | ❌ | ❌ | ✅ | ✅ |

C++20 "transient allocation" — `new` in a `constexpr` function is allowed if the allocation is freed before the function returns. The allocated memory never makes it into the binary.

---

## 3. `if constexpr` — HOW THE COMPILER HANDLES IT

### Two-Phase Lookup (Critical to Understand)
C++ templates undergo two-phase name lookup:
1. **Phase 1 (definition time):** names that don't depend on template params are resolved immediately.
2. **Phase 2 (instantiation time):** names that depend on template params are resolved.

`if constexpr` operates at instantiation time. The *discarded branch* undergoes minimal syntactic checking (still must be parseable) but is **not instantiated** — member functions are not looked up, expressions are not type-checked.

```cpp
template <typename T>
void process(T val) {
    if constexpr (std::is_integral_v<T>) {
        val.nonexistent_method();   // NOT a compile error — branch is discarded for T=int
    } else {
        return val;
    }
}
// process(42) is valid — the first branch is taken, second is discarded
// process("hi") would fail — second branch can't return val for string
```

### Endian Swap Pattern (used in market data parsers)
```cpp
#include <bit>          // C++20 std::endian

template <typename T>
T from_network(T val) {
    if constexpr (std::endian::native == std::endian::little) {
        return __builtin_bswap64(val);   // only compiled on little-endian
    } else {
        return val;                       // no-op on big-endian
    }
}
```
The compiler emits zero branch instructions — just `bswap` or the identity, with dead code completely removed.

---

## 4. SFINAE — MECHANICS AND PATTERNS

### How Substitution Works
When the compiler encounters a function template call:
1. Deduce template args from the call.
2. Substitute args into the template signature.
3. If substitution produces an invalid type → remove this overload (no error).
4. If no valid overloads remain → error "no matching function."

**What counts as substitution failure (safe — SFINAE):**
- Invalid type: `typename enable_if<false>::type`
- Invalid expression in immediate context: `decltype(t.nonexistent())`
- Type mismatch in default template args

**What does NOT count (hard error):**
- Errors inside the function body
- Errors in a class member that's not in immediate substitution context

### `enable_if` in Three Syntactic Forms
```cpp
// Form 1: default template parameter (preferred for classes)
template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
struct IntWrapper { T val; };

// Form 2: return type (for functions, non-ambiguous overloads)
template <typename T>
std::enable_if_t<std::is_integral_v<T>, T> add(T a, T b) { return a+b; }

// Form 3: extra parameter (avoids return-type complexity)
template <typename T>
T multiply(T a, T b, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr);
```

### `void_t` — Detection Pattern
```cpp
// Detect if T has a .submit() method
template <typename T, typename = void>
struct is_submittable : std::false_type {};

template <typename T>
struct is_submittable<T, std::void_t<decltype(std::declval<T>().submit())>>
    : std::true_type {};
```

`std::declval<T>()` is a declaration-only function that returns `T&&`. It lets you "use" an object of type `T` in unevaluated contexts (like `decltype`) without constructing one.

### Detection Idiom (cppreference / Herb Sutter N4502)
```cpp
struct nonesuch {
    nonesuch() = delete;
    ~nonesuch() = delete;
    nonesuch(const nonesuch&) = delete;
    void operator=(const nonesuch&) = delete;
};

template <typename Default, typename AlwaysVoid,
          template <typename...> class Op, typename... Args>
struct detector { using type = Default; static constexpr bool value = false; };

template <typename Default, template <typename...> class Op, typename... Args>
struct detector<Default, std::void_t<Op<Args...>>, Op, Args...> {
    using type = Op<Args...>;
    static constexpr bool value = true;
};

template <template <typename...> class Op, typename... Args>
using is_detected = typename detector<nonesuch, void, Op, Args...>::type;

// Usage
template <typename T> using has_submit_t = decltype(std::declval<T>().submit());
// is_detected<has_submit_t, MyOrder>::value == true if MyOrder has .submit()
```

---

## 5. TYPE TRAITS — INTERNALS

All type traits are just struct specializations:

```cpp
// How is_integral is implemented (conceptually):
template <typename T> struct is_integral_base : std::false_type {};
template <> struct is_integral_base<bool>           : std::true_type {};
template <> struct is_integral_base<char>           : std::true_type {};
template <> struct is_integral_base<int>            : std::true_type {};
template <> struct is_integral_base<long>           : std::true_type {};
// … and so on for all integral types
template <typename T>
struct is_integral : is_integral_base<std::remove_cv_t<T>> {};
```

**Zero runtime cost:** type traits are pure compile-time. They produce a `constexpr bool` value embedded in the code as a literal. No function call, no memory access.

### `std::decay<T>` — What It Models
`decay` mimics the transformation applied to function parameters when passed by value:
1. Remove references: `T&` → `T`, `T&&` → `T`
2. Remove cv-qualifiers: `const T` → `T`
3. Arrays: `T[N]` → `T*`
4. Functions: `int(int)` → `int(*)(int)`

This is exactly what `auto x = expr;` deduces. Use `decay_t` when you want to store a value type from a potentially-reference parameter.

---

## 6. VARIADIC TEMPLATES — ADVANCED PATTERNS

### TypeList — Compile-Time Type Sequence
```cpp
template <typename... Ts> struct TypeList {};
using MarketTypes = TypeList<SpotOrder, FuturesOrder, OptionOrder>;
```

**Useful operations on TypeList:**
```cpp
// Length
template <typename List> struct Length;
template <typename... Ts>
struct Length<TypeList<Ts...>> : std::integral_constant<size_t, sizeof...(Ts)> {};

// Head / Tail
template <typename List> struct Head;
template <typename H, typename... Ts>
struct Head<TypeList<H, Ts...>> { using type = H; };

// Contains (fold expression approach)
template <typename T, typename... Ts>
constexpr bool contains = (std::is_same_v<T, Ts> || ...);
```

### `std::tuple` Traversal (apply pattern)
```cpp
template <typename Tuple, typename F, std::size_t... Is>
void for_each_impl(Tuple& t, F&& f, std::index_sequence<Is...>) {
    (f(std::get<Is>(t)), ...);   // fold expression calls f on each element
}

template <typename Tuple, typename F>
void for_each(Tuple& t, F&& f) {
    for_each_impl(t, std::forward<F>(f),
        std::make_index_sequence<std::tuple_size_v<Tuple>>{});
}
```

**Use case:** iterate fields of an HFT message struct represented as a tuple, calling a serialization function on each field — zero runtime overhead, fully unrolled.

---

## 7. C++20 CONCEPTS — PRODUCTION PATTERNS

### Concept Hierarchy for Order Types
```cpp
concept Instrument = requires(T t) {
    { t.symbol() } -> std::same_as<std::string_view>;
    { t.tick_size() } -> std::same_as<double>;
};

concept Order = Instrument<T> && requires(T t) {
    { t.side() }     -> std::same_as<Side>;
    { t.quantity() } -> std::integral;
};

concept LimitOrder = Order<T> && requires(T t) {
    { t.limit_price() } -> std::same_as<double>;
};
```

The compiler uses subsumption: `LimitOrder` subsumes `Order` which subsumes `Instrument`. In overload resolution, the most specific concept wins.

### Constrained Class Templates
```cpp
template <Order O>
class RiskEngine {
    void check(const O& order) {
        // Compiler KNOWS O has .side(), .quantity() — no SFINAE noise
        if (order.quantity() > max_size_) reject(order);
    }
};
```

### `requires` Clause vs `requires` Expression
```cpp
// requires clause: a constraint applied to a template
template <typename T> requires std::integral<T>
T safe_divide(T a, T b);

// requires expression: tests validity of expressions → used in concept definitions
template <typename T>
concept HasPrice = requires(T t) {
    t.price();                              // expression must be valid
    { t.price() } -> std::same_as<double>; // and have this return type
};
```

---

## 8. CRTP — DEEP DIVE

### How the Call Resolves
```cpp
template <typename D>
struct Logger {
    void log(const char* msg) {
        // static dispatch — no virtual table
        static_cast<D*>(this)->log_impl(msg);
    }
};

struct FileLogger : Logger<FileLogger> {
    void log_impl(const char* msg) { /* write to file */ }
};
```

Assembly (simplified):
```asm
; FileLogger::log(msg):
; Compiler inlines static_cast<FileLogger*>(this)->log_impl(msg)
; → equivalent to:
call FileLogger::log_impl   ; direct call, no indirect through vtable
```

With `virtual`:
```asm
; BaseLogger::log(msg):
mov rax, [rdi]              ; load vtable pointer from object
call [rax + offset]         ; indirect call through vtable → branch predictor miss likely
```

**The 5–10 ns difference** comes from: vtable pointer load (cache miss if object is cold) + indirect branch (branch predictor must predict target, fails on first call and after eviction).

### Mixin CRTP (multiple policies)
```cpp
template <typename D>
struct LatencyLogger {
    void on_order_sent() { static_cast<D*>(this)->record_latency(); }
};

template <typename D>
struct RiskGuard {
    bool check(double notional) { return static_cast<D*>(this)->check_impl(notional); }
};

struct FastTrader
    : LatencyLogger<FastTrader>
    , RiskGuard<FastTrader> {
    void record_latency() { /* ... */ }
    bool check_impl(double n) { return n < 1e6; }
};
```

All calls inlined, zero virtual overhead, multiple inheritance layout is trivial (no diamond).

---

## 9. COMPILE-TIME LOOKUP TABLES — HFT PRODUCTION PATTERN

### Price Tick Table
```cpp
struct TickEntry {
    int     instrument_id;
    double  tick_size;
    int     lot_size;
};

constexpr TickEntry kTickTable[] = {
    { 1001, 0.01,  100  },   // AAPL
    { 1002, 0.05,  10   },   // BRK.B
    { 2001, 0.25,  1    },   // ES futures
    { 2002, 0.10,  1    },   // NQ futures
};

constexpr const TickEntry* find_tick(int id) {
    for (const auto& e : kTickTable)
        if (e.instrument_id == id) return &e;
    return nullptr;
}

static_assert(find_tick(1001)->tick_size == 0.01);
static_assert(find_tick(2001)->lot_size == 1);
```

At link time this becomes:
- `kTickTable` → 4 × 24 bytes in `.rodata` — loaded once, stays in L1 cache all session.
- `find_tick` called with a compile-time constant → inlined to a single `lea` or `mov`.

### Perfect-Hash Lookup (zero branch)
For a known, fixed set of instrument IDs, a perfect hash can map ID → index in O(1) with no branches:
```cpp
constexpr size_t hash_id(int id) {
    return static_cast<size_t>(id) % kTickTable_size;  // pick modulus at compile time
}
constexpr auto kFastTable = build_perfect_hash(kTickTable);  // constexpr algorithm
```

The compiler verifies no collisions exist via `static_assert`. The hot-path lookup is a single integer modulo + array access — faster than any switch statement.

---

## 10. TEMPLATE METAPROGRAMMING — BEFORE AND AFTER C++17

### Before C++17: Recursion
```cpp
// Compute sum of 1..N at compile time
template <int N>
struct Sum { static constexpr int value = N + Sum<N-1>::value; };
template <>
struct Sum<0> { static constexpr int value = 0; };

static_assert(Sum<100>::value == 5050);
```

Limitation: deep recursion hits compiler limits (usually 900–1024 levels). Large TMP programs cause compile-time explosions.

### After C++17: constexpr + fold expressions
```cpp
constexpr int sum(int n) {
    int acc = 0;
    for (int i = 1; i <= n; i++) acc += i;
    return acc;
}
static_assert(sum(100) == 5050);
```

Same result, but:
- Readable as normal C++.
- Compile times are orders of magnitude faster.
- No recursion depth limit.
- Debuggable (constexpr functions can be stepped through in debuggers).

**Rule of thumb:** prefer `constexpr` functions over TMP structs for any computation. Use TMP structs only for type manipulation (TypeList, conditional, enable_if).

---

## 11. COMPILATION SPEED — THE HIDDEN COST

Heavy template instantiation is the #1 cause of slow C++ build times in HFT codebases.

**Techniques to manage it:**
1. **`extern template`**: explicitly instantiate in one TU, suppress in others.
   ```cpp
   // header
   extern template class PriceEngine<SpotOrder>;
   // one .cpp
   template class PriceEngine<SpotOrder>;
   ```

2. **Prefer `constexpr` over TMP** — faster to evaluate (see above).

3. **Avoid SFINAE cascades** — each failed substitution costs compile time. Convert to Concepts.

4. **Unity builds**: combine TUs into one compilation unit — amortizes template instantiation over the project.

5. **C++20 Modules**: replace `#include` with `import` — headers compiled once, not per-TU. 10–50× compile speedup on template-heavy code. Most major compilers support modules experimentally.

---

## 12. FIXED STRING — COMPILE-TIME STRING LITERAL

A common HFT pattern: instrument symbols (e.g., "AAPL", "ES") need to be compared, hashed, and stored. Using `std::string` → heap allocation. Using `const char*` → loses length. A compile-time FixedString:

```cpp
template <size_t N>
struct FixedString {
    char data[N] = {};
    constexpr FixedString(const char (&s)[N]) {
        for (size_t i = 0; i < N; i++) data[i] = s[i];
    }
    constexpr bool operator==(const FixedString&) const = default;
    constexpr std::string_view view() const { return {data, N-1}; }
};

// C++20: NTTPs can be class types
template <FixedString Symbol>
struct Instrument {
    static constexpr auto name = Symbol;
};

using AAPL = Instrument<"AAPL">;
using ES   = Instrument<"ES">;
// AAPL::name.view() == "AAPL"   — no heap, no runtime cost
```

The `FixedString` lives in the type system itself. Different symbols produce different types — the compiler can specialize functions per symbol.

---

## 13. POLICY-BASED DESIGN — ZERO-OVERHEAD STRATEGY PATTERN

```cpp
struct NullLogger {
    static void log(const char*) {}   // inlined to nothing
};

struct ConsoleLogger {
    static void log(const char* msg) { puts(msg); }
};

struct NullRisk {
    static bool check(double) { return true; }  // inlined to true
};

struct FirmRisk {
    static bool check(double notional) { return notional < 1e7; }
};

template <
    typename Logger  = NullLogger,
    typename Risk    = NullRisk
>
class OrderRouter {
public:
    void send(double notional, const char* sym) {
        if (!Risk::check(notional)) { Logger::log("REJECTED"); return; }
        Logger::log(sym);
        // … send order
    }
};

using ProdRouter  = OrderRouter<NullLogger,   FirmRisk>;     // no logging overhead
using TestRouter  = OrderRouter<ConsoleLogger, NullRisk>;    // full logging, no risk
```

The compiler inlines all static methods. `ProdRouter::send()` with `NullLogger` emits zero logging code — literally not present in the binary. This is "pay for what you use" at the binary level.

---

## 14. SUMMARY: COMPILE-TIME VS RUNTIME COSTS

| Pattern | Runtime cost | Notes |
|---|---|---|
| `constexpr` variable | **0 ns** | literal in `.rodata` |
| `if constexpr` branch | **0 ns** | discarded branch not emitted |
| CRTP virtual-like dispatch | **0–0.3 ns** | direct/inlined call |
| Concept constraint check | **0 ns** | compile-time only |
| `std::enable_if` | **0 ns** | compile-time only |
| CT lookup table (array) | **~1 ns** | single cache-line read |
| Policy template (NullLogger) | **0 ns** | completely dead-code-eliminated |
| Virtual function call | **5–10 ns** | vtable + indirect branch |
| `dynamic_cast` | **10–50 ns** | RTTI traversal |
| `std::function` call | **10–50 ns** | type erasure + indirect call |

For HFT hot paths, the top half of this table is the only acceptable zone.
