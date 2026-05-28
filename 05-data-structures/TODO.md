# Chapter 4 — TODO Task List
> C++ High Performance (2nd Ed.) | **Data Structures**
> Folder: `cpp-high-performance/05-data-structures/`
> Linked code: `code/01_sequence_containers.cpp`, `code/02_associative_containers.cpp`, `code/03_unordered_containers.cpp`, `code/04_container_adaptors.cpp`, `code/05_soa_vs_aos.cpp`, `code/06_ringbuffer_spsc.cpp`, `code/07_orderbook_pricelevel.cpp`
> Linked repos: [PacktPublishing/Cpp-High-Performance-Second-Edition](https://github.com/PacktPublishing/Cpp-High-Performance-Second-Edition) (Chapter04) · [ITHelpDec/CPP-High-Performance](https://github.com/ITHelpDec/CPP-High-Performance) · [sh-arka22/Low-Latency-CPP-HighPerformance](https://github.com/sh-arka22/Low-Latency-CPP-HighPerformance)

---

## ✅ HOW TO USE THIS FILE
- BFS first — sweep every topic at **Level 1** so you have a working concept map.
- Then DFS — drop into **Level 2** (apply) and **Level 3** (HFT challenge) for the topics that matter most for trading.
- Tick `[ ]` → `[x]` as you go. Each topic also has an "interviewer would ask" prompt.

---

## TOPIC 1 — The Properties Standard Containers Must Provide

### Level 1 — Understand
- [ ] Read the chapter intro: name the three **container concepts** (Sequence, Associative, Unordered Associative) and one **container adaptor**.
- [ ] List the four guarantees every std container offers: (a) value semantics, (b) iterator support, (c) `size()`/`empty()`, (d) allocator awareness.
- [ ] Explain in one sentence why **moves are noexcept for most STL containers** — and why noexcept moves matter for `std::vector::push_back` reallocation.
- [ ] Define **iterator invalidation**. Which operations invalidate iterators in `vector`, `deque`, `map`, `unordered_map`?

### Level 2 — Apply
- [ ] Implement a tiny `concept SequenceContainer` (C++20) that requires `begin()`, `end()`, `size()`, `push_back()`, and value semantics — instantiate it for `std::vector` and `std::list`.
- [ ] Write a snippet that **proves iterator invalidation**: push into a `vector` while holding a pointer to `front()`. Run under sanitizer (`-fsanitize=address`).

### Level 3 — HFT Challenge
- [ ] Audit the order-book code (you'll write later) and **classify every container choice** against the four guarantees. Where is value semantics costing you? Where is iterator stability hurting you?

> **Interviewer asks:** "Which standard containers preserve iterators when you insert? Which preserve pointers to elements?"

---

## TOPIC 2 — Sequence Containers (vector, array, deque, list, forward_list)

### Level 1 — Understand
- [ ] Open `code/01_sequence_containers.cpp` and read each section.
- [ ] Explain the **3-pointer layout** of `std::vector`: `begin`, `end`, `end_of_capacity`. What is `sizeof(std::vector<int>)` on a 64-bit system?
- [ ] What is the growth factor used by libstdc++ (gcc)? By MSVC? Why is the **golden ratio (~1.618)** theoretically nice but rarely chosen?
- [ ] Explain `std::deque`'s **chunked / map-of-blocks** layout. Why does iterating a deque cost more than iterating a vector even when both fit in cache?
- [ ] Why is `std::list` "almost never the right answer" in modern C++? (Cache misses, allocator pressure, no random access.)
- [ ] When is `std::array<T,N>` the right answer? (Compile-time size, no heap, stack-allocated, `constexpr`-friendly.)

### Level 2 — Apply
- [ ] Print `sizeof(std::vector<int>)`, `sizeof(std::deque<int>)`, `sizeof(std::list<int>)`, `sizeof(std::array<int,16>)` and explain each number.
- [ ] Instrument a `std::vector<int>` doing 1M `push_back`s — log every change of `capacity()`. Confirm the growth factor your STL uses (2× on gcc, 1.5× on MSVC).
- [ ] Compare iteration time of `std::vector<int>` vs `std::list<int>` for 10M `int`s. Explain the ratio you observe in terms of cache lines.

### Level 3 — HFT Challenge
- [ ] Write a `FixedVector<T, N>` (a.k.a. `static_vector`) that lives on the stack — `std::array<T,N>` buffer + an integer `size_`. No heap, no exceptions. Used in hot paths where N is bounded.
- [ ] Benchmark `FixedVector<int,1024>` vs `std::vector<int>` for 1M push/pop cycles. Measure ns/op and allocation count.

> **Interviewer asks:** "I have an in-memory event log of 50M events. Which container? Why?" (Answer: `std::vector` with `reserve()`. *Not* deque — chunk-jump kills cache; *not* list — pointer chase = catastrophe.)

---

## TOPIC 3 — Associative Containers (set, map, multiset, multimap)

### Level 1 — Understand
- [ ] Open `code/02_associative_containers.cpp` and trace each example.
- [ ] State the **invariant** of `std::map`: a strict-weak-ordering by key. Internally it's a **red-black tree** (height ≤ 2·log₂(n+1)).
- [ ] Insert/lookup/erase complexity: O(log n). Why log and not constant? (Tree height.)
- [ ] How does `std::map` compare to a B-tree like `absl::btree_map`? Why are B-trees more cache-friendly?
- [ ] What does **`lower_bound` / `upper_bound`** return and when do you reach for them?
- [ ] Why does `operator[]` on `std::map` **default-construct** missing values? When is that a bug magnet?

### Level 2 — Apply
- [ ] Implement a `PriceLadder` keyed by price (int ticks) using `std::map<int,int>` — insert 1M random levels and time `lower_bound` lookups.
- [ ] Compare `std::map<int,int>` vs `absl::btree_map<int,int>` (or `boost::container::flat_map`) on the same workload — measure lookups/sec.
- [ ] Demonstrate that **iterators to `std::map` remain valid** across insert/erase of *other* keys — push into the map while holding an iterator, prove it still works.

### Level 3 — HFT Challenge
- [ ] Build a `BookSide` class using `std::map<int, Level, std::greater<int>>` for bids and `std::map<int, Level>` for asks — implement `best_bid()` / `best_ask()` in O(1) via `begin()`.
- [ ] Replace the `std::map` with a `boost::container::flat_map` and rebenchmark — when does the flat structure win and when does it lose?

> **Interviewer asks:** "Why is `std::map` typically slower than a B-tree for the same operations even though both are O(log n)?" (Answer: cache lines. Red-black nodes hold one key per cache line; B-tree nodes hold many keys per cache line → fewer cache misses per descent.)

---

## TOPIC 4 — Unordered Associative Containers (unordered_set / unordered_map)

### Level 1 — Understand
- [ ] Open `code/03_unordered_containers.cpp` and trace each example.
- [ ] Describe the **separate-chaining** layout the standard mandates: bucket array → singly-linked list of nodes. Why does the standard *require* this? (Iterator stability + bucket interface.)
- [ ] Define **load factor** = size / bucket_count. What is the default `max_load_factor` (= 1.0)?
- [ ] When does the table **rehash**? What's the complexity (amortized O(1) per insert, but a single insert can be O(n) on rehash)?
- [ ] Why is `std::unordered_map` **slower than `absl::flat_hash_map`** in practice, even though both are O(1)? (Cache: separate chaining → pointer chase; open addressing → linear probe in one cache line.)

### Level 2 — Apply
- [ ] Print `bucket_count()` and `load_factor()` after each of 10 `reserve()` doublings.
- [ ] Write a custom hash for a struct `OrderId{ uint64_t client; uint32_t local; };` — verify with a counter that the distribution is roughly uniform.
- [ ] Show that **using `[]` on a missing key inserts a default-constructed value** — and how `.find()` avoids that bug.

### Level 3 — HFT Challenge
- [ ] Implement a tiny **open-addressing linear-probing** hash map `LinearMap<K,V,N>` with power-of-two capacity, no heap, no exceptions. Compare its lookup throughput against `std::unordered_map<K,V>` on a 1M-key workload.
- [ ] Add **Robin Hood probing** to your map and re-benchmark. Explain the variance reduction.

> **Interviewer asks:** "Walk me through what happens in memory when I call `m.insert({k,v})` on a `std::unordered_map`. Where does the node live? How many cache lines do you touch on a lookup?"

---

## TOPIC 5 — Container Adaptors (stack, queue, priority_queue)

### Level 1 — Understand
- [ ] Open `code/04_container_adaptors.cpp` and read each example.
- [ ] State the **default underlying container** for each adaptor: `stack` → `deque`, `queue` → `deque`, `priority_queue` → `vector` (with `std::make_heap`/`push_heap`/`pop_heap`).
- [ ] Why is `priority_queue<T, std::vector<T>>` faster than `priority_queue<T, std::deque<T>>` on most workloads?
- [ ] Explain how `std::push_heap` and `std::pop_heap` work on a vector — what is the array layout of a binary heap?

### Level 2 — Apply
- [ ] Build a `std::priority_queue<Order, std::vector<Order>, OrderComparator>` keyed by price-time priority. Push 100k orders, pop them all in order, verify monotonicity.
- [ ] Show that `std::stack<int, std::vector<int>>` is faster than `std::stack<int, std::deque<int>>` for 10M push/pop ops — and explain why (locality).

### Level 3 — HFT Challenge
- [ ] Build a **bounded `RingBufferQueue<T, N>`** (single-producer-single-consumer) and use it inside `std::queue` via the third template parameter — compare with default `std::deque` backend.

> **Interviewer asks:** "Why is `priority_queue` `vector`-backed by default rather than `list` or `deque`?"

---

## TOPIC 6 — Parallel Arrays (SoA) vs AoS

### Level 1 — Understand
- [ ] Define **Array of Structures (AoS)** vs **Structure of Arrays (SoA)** with a 1-line example each.
- [ ] Why is SoA usually faster for **vectorizable** code (SIMD, hot loop touches one field)? Why is AoS usually friendlier for **OO** code (whole-object operations)?
- [ ] What is the **80% rule**: if you read fewer than ~80% of the bytes of a struct in a tight loop, you're paying for unused cache lines.

### Level 2 — Apply
- [ ] Open `code/05_soa_vs_aos.cpp`. Implement `struct Order` and `struct OrdersSoA` (parallel `std::vector` per field). Time a sum-over-prices loop. SoA should win by 2-5×.
- [ ] Use `perf stat -e cache-misses,cache-references` on both — show that SoA has dramatically fewer L1-d misses.

### Level 3 — HFT Challenge
- [ ] Add `std::execution::par_unseq` (or OpenMP) to your SoA price-sum and benchmark. SIMD + parallel = near memory-bandwidth limit.
- [ ] Refactor an existing AoS class in the repo into SoA without changing its public API (proxy iterator). Measure speedup.

> **Interviewer asks:** "I have 100M trades, each with 8 fields, and I want to compute average price. Lay out the data."

---

## TOPIC 7 — Cache-Line Alignment & False Sharing

### Level 1 — Understand
- [ ] Define **cache line** (typically 64 B on x86, 128 B on Apple M-series).
- [ ] Define **false sharing**: two threads write to two variables that happen to share a cache line — coherence traffic kills throughput.
- [ ] Use `std::hardware_destructive_interference_size` (C++17) — what does it return on your machine?

### Level 2 — Apply
- [ ] Write two counters in a struct, hammer them from two threads — measure throughput. Now `alignas(64)` each one — measure again. Should be 5-50× faster.

### Level 3 — HFT Challenge
- [ ] In your SPSC ring buffer (Topic 8), the producer's head index and the consumer's tail index **must** live on different cache lines. Prove this with a benchmark by deliberately co-locating them and watching latency explode.

> **Interviewer asks:** "What is false sharing? Show me how you'd fix it in a struct with two atomic counters."

---

## TOPIC 8 — Lock-Free SPSC Ring Buffer (HFT staple)

### Level 1 — Understand
- [ ] Open `code/06_ringbuffer_spsc.cpp`. Read the implementation.
- [ ] Why does power-of-two capacity let you replace `index % N` with `index & (N-1)`? (Single AND vs division.)
- [ ] What memory order does the **producer** use to publish a write? (`memory_order_release` on head store.)
- [ ] What memory order does the **consumer** use to read? (`memory_order_acquire` on head load.)
- [ ] Why is acquire/release enough — why not `seq_cst`? (No cross-variable ordering needed; cheaper barrier.)

### Level 2 — Apply
- [ ] Run the included test: producer pushes 10M ints, consumer drains them, verify the sum.
- [ ] Move `head_` and `tail_` into the same struct without `alignas(64)`. Re-run and observe latency rise — that's false sharing.

### Level 3 — HFT Challenge
- [ ] Extend to **MPMC** (multi-producer, multi-consumer) using compare-and-swap on head/tail. Discuss why MPMC is dramatically harder than SPSC.
- [ ] Add an "ABA-safe" version using tagged pointers or 64-bit sequence numbers.

> **Interviewer asks:** "Implement an SPSC queue with `std::atomic` and tell me which memory orderings you'd use and why."

---

## TOPIC 9 — Order-Book Data Structures (the HFT case study)

### Level 1 — Understand
- [ ] What is a **price-time-priority** order book? (FIFO queue per price level; sorted price levels.)
- [ ] Why is `std::map<Price, Level>` the textbook implementation? Why is it too slow for production HFT?
- [ ] Three production designs: (a) **sorted `std::vector` of levels + binary search**, (b) **array indexed by price ticks** (when price range is bounded), (c) **intrusive linked lists** for orders inside each level.

### Level 2 — Apply
- [ ] Open `code/07_orderbook_pricelevel.cpp`. Implement a `Book` with `std::map<Price, Level, std::greater<>>` for bids and `std::map<Price, Level>` for asks.
- [ ] Implement `add_order`, `cancel_order`, `match`. Verify with a small replay.

### Level 3 — HFT Challenge
- [ ] Switch to an **array-indexed book**: prices are int ticks in [0, MAX_TICKS), so each level lives at `levels[price_tick]`. O(1) best-of-side via a tracked top-of-book pointer.
- [ ] Benchmark map-based vs array-based book on 1M add+match operations. Report median and p99 latency.
- [ ] Add **intrusive doubly-linked list** of orders per price level so cancels are O(1) given an order pointer.

> **Interviewer asks:** "Design an order book in C++. What's your data structure choice for the price-level container? Why not just `std::map`?"

---

## TOPIC 10 — Allocators & Memory Pools (preview of Chapter 7)

### Level 1 — Understand
- [ ] Why does every STL container take an allocator template parameter?
- [ ] What is a **pool allocator** and when is it the right answer? (Fixed-size, lock-free, deterministic latency.)
- [ ] What is C++17's `std::pmr` (polymorphic memory resources)?

### Level 2 — Apply
- [ ] Wrap your `std::unordered_map` in `std::pmr::unordered_map` backed by `std::pmr::monotonic_buffer_resource` of size 1 MB on the stack. Confirm with `::operator new` instrumentation that **zero heap allocations** occur for inserts that fit.

### Level 3 — HFT Challenge
- [ ] Implement a `PoolAllocator<T, N>` (chunk-based, intrusive free list). Plug it into a `std::list` and verify the entire list of N elements never touches the heap after construction.

> **Interviewer asks:** "Why would you write a custom allocator for an STL container in a trading system?"

---

## Done conditions
- [ ] All 10 topics have **at least Level 1 ticked**.
- [ ] At least 3 topics have **Level 3 (HFT) ticked**.
- [ ] `code/` builds cleanly with `g++ -std=c++20 -O2 -Wall -Wextra`.
- [ ] You can answer every "Interviewer asks" prompt out loud, on paper, in under 90 seconds.

---

*Linked deep-dive notes: [`NOTES.md`](./NOTES.md) · [`05_data_structures_deep_dive.md`](./05_data_structures_deep_dive.md)*
