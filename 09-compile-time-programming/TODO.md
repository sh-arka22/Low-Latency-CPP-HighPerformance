# Chapter 9 — Compile-Time Programming: TODO
> C++ High Performance (2nd Ed.) | Björn Andrist & Viktor Sehr
> Repo folder: `cpp-high-performance/09-compile-time-programming/`

---

## WHY THIS CHAPTER MATTERS FOR HFT
Compile-time programming moves computation from runtime to compile time — zero CPU cycles on the hot path. HFT firms use it to build:
- Compile-time lookup tables (tick-to-price arrays, instrument maps)
- Zero-overhead type dispatch (no `virtual`, no vtable)
- Policy-based designs (strategy, allocator, logger all selected at compile time)
- Constrained generic code that catches bugs at compile time instead of prod

---

## LEVEL 1 — Core Concepts (must know cold)

- [ ] **L1.1** — `constexpr` functions: what makes a function constexpr-eligible, when it actually runs at compile time vs runtime
- [ ] **L1.2** — `consteval` (C++20): guaranteed compile-time evaluation, cannot be called at runtime
- [ ] **L1.3** — `constinit` (C++20): static-init order fiasco prevention; variable must be const-initialized but is NOT immutable
- [ ] **L1.4** — `if constexpr`: compile-time branching; discarded branch is NOT instantiated (critical difference vs regular if)
- [ ] **L1.5** — Template specialization: full vs partial; what "most specialized" means; ADL interaction
- [ ] **L1.6** — Type traits: `std::is_*`, `std::conditional`, `std::decay`, `std::remove_reference`, custom traits via `false_type`/`true_type`

---

## LEVEL 2 — Intermediate Techniques (interview gold)

- [ ] **L2.1** — SFINAE ("Substitution Failure Is Not An Error"): how the overload set is pruned; `std::enable_if<>` and `void_t<>` patterns
- [ ] **L2.2** — C++20 Concepts: `concept`, `requires`, constrained `auto`, `requires requires`; why they beat SFINAE (readable errors, better diagnostics)
- [ ] **L2.3** — Variadic templates: parameter packs, `sizeof...`, fold expressions (C++17), recursive unpacking
- [ ] **L2.4** — Non-type template parameters (NTTPs): integer, pointer, `auto` (C++17), string literal (C++20)
- [ ] **L2.5** — `std::integral_constant`, tag dispatch pattern: zero-overhead branch selection with no virtual calls

---

## LEVEL 3 — HFT / Advanced (deep dives, production patterns)

- [ ] **L3.1** — Compile-time lookup tables: `constexpr` arrays, hash maps; tick-size table with 0-ns lookup
- [ ] **L3.2** — `constexpr` string / FixedString: interned symbol names at compile time, no heap allocation
- [ ] **L3.3** — Policy-based design: compile-time strategy injection (logger, allocator, risk check) with zero virtual overhead
- [ ] **L3.4** — Template Metaprogramming (TMP) recursion before C++17 vs fold expressions; `std::tuple` traversal
- [ ] **L3.5** — CRTP (Curiously Recurring Template Pattern): static polymorphism, mixin design; `operator+` on a vector base without virtual dispatch
- [ ] **L3.6** — `std::conditional_t`, `std::enable_if_t`, `std::void_t`, detection idiom — building compile-time capability probes
- [ ] **L3.7** — Compilation speed trade-offs: heavy TMP increases compile time; `extern template`, unity builds, modular headers (C++20 modules preview)

---

## CODE TASKS
- [ ] `code/01_constexpr_basics.cpp` — constexpr, consteval, constinit; CT vs RT dispatch proof
- [ ] `code/02_template_specialization.cpp` — full/partial spec, tag dispatch, CRTP mixin
- [ ] `code/03_sfinae_enable_if.cpp` — SFINAE mechanics, enable_if, void_t, detection idiom
- [ ] `code/04_type_traits.cpp` — std::is_*, conditional, decay, custom trait; `if constexpr` usage
- [ ] `code/05_variadic_templates.cpp` — packs, fold expressions, TypeList, index_sequence
- [ ] `code/06_concepts.cpp` — concept syntax, requires, constrained auto, concept hierarchy
- [ ] `code/07_tmp_hft_patterns.cpp` — CT lookup table, FixedString, policy-based order router

---

## INTERVIEWER ASKS
> "What's the difference between `constexpr` and `consteval`?"
> "When does `if constexpr` discard a branch?"
> "Why would you use CRTP over virtual functions in a trading system?"
> "How do C++20 Concepts improve on SFINAE?"
> "Show me a compile-time lookup table for tick sizes."
> "What is the static-initialization order fiasco and how does `constinit` help?"
