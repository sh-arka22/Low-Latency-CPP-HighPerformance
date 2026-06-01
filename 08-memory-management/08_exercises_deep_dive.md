# Chapter 08 — Memory Management: Deep Dive Exercise Questions
> Focus: HFT / Low-Latency C++ · Sub-microsecond Latency Thinking
> Author notes: These are not syntax recall questions — they are *reasoning under latency pressure* exercises.
> Treat every question as an interview question at a quant trading firm.

---

## Section 1 — Memory Hierarchy & Latency Intuition

---

### Q1. The Latency Ruler (Reasoning)

You are writing a market-data handler that decodes 1 million UDP packets per second. Each packet decode accesses an order object. You have two design choices:

- **Design A:** The `Order` struct is fetched from DRAM (~70 ns per access) because it was allocated on the heap and is randomly scattered.
- **Design B:** All `Order` objects are packed into a contiguous pre-allocated array that fits in L2 cache (~4 ns per access).

**Question:** By how much does Design B reduce the total time spent in memory access over 1 second? Show your arithmetic. Why does this matter for a strategy that must respond within 100 ns of receiving a packet?

> **Hint:** Compute: accesses/sec × latency/access for each design. Total budget for the hot path is 100 ns. Where does 66 ns of savings per tick go?
>
> **Think about:** L2 cache size is ~256 KB–1 MB per core. If `sizeof(Order) = 64` bytes and you have 4096 outstanding orders, will they fit? What happens to latency when the working set is just slightly larger than L2?

---

### Q2. NUMA Topology Gotcha (Architecture)

Your trading server has 2 NUMA sockets. Your order-book processing thread is pinned to CPU 0 (socket 0). A junior developer allocates the order-book hash map using `new` from the main thread (which runs on socket 1 at startup).

**Question:** What latency penalty do you expect on every order-book lookup? Express it in nanoseconds and in CPU cycles (assume 3 GHz clock). How would you fix this with Linux system calls?

> **Hint:** Remote NUMA access = ~140 ns. Local = ~70 ns. The fix involves `numa_alloc_onnode()` or `mmap + mbind`. The thread doing the allocation must be the thread that will *use* the memory — or you must migrate the memory explicitly.
>
> **Think about:** `numactl --membind=0 ./your_binary` — what does this do? Is it sufficient for a multi-threaded app where threads are dynamically spawned?

---

### Q3. The "First Touch" Ambush (Page Faults)

At 9:30 AM when the market opens, your system receives its first burst of 10,000 orders. You've pre-allocated a 64 MB arena using `mmap(MAP_ANONYMOUS)` at startup. The first tick takes 8 µs instead of the expected 100 ns.

**Question:** What is causing the latency spike? Why does `mmap` not immediately back the memory with physical pages? How do you fix this for production?

> **Hint:** `mmap` reserves virtual address space, but physical pages are only assigned on first write (demand paging). A page fault involves a kernel trap (~1–10 µs each). The fix: `memset(buf, 0, size)` or `madvise(MADV_POPULATE_WRITE)` at startup to pre-fault all pages. Also consider `mlockall(MCL_CURRENT | MCL_FUTURE)`.
>
> **Think about:** Why should `mlockall` be called as a RAII object? What happens if the process exits without calling `munlockall`? Is it actually required — what does the OS do on process exit?

---

## Section 2 — Stack vs Heap Deep Mechanics

---

### Q4. The Assembly Difference (CPU Mechanics)

Explain, at the assembly instruction level, why allocating a 64-byte object on the stack costs ~0.3 ns but the same allocation on the heap via `malloc` can cost 50–200 ns. Name at least four distinct sources of latency that `malloc` introduces that the stack does not.

> **Hint:**
> - Stack: single `sub rsp, 64` instruction. RSP is already in a register — no memory access needed.
> - Heap sources: (1) free list search in size bins, (2) coalescing on `free`, (3) potential kernel syscall for new pages, (4) arena lock for multi-threaded arena selection.
>
> **Think about:** What instruction does `free` emit when it coalesces adjacent blocks? When does glibc call `sbrk()` vs `mmap()`? (Answer: `mmap` for >128 KB allocations — check `/proc/self/maps` at runtime to verify.)

---

### Q5. Stack Overflow by Design (Sizing)

You want to process a market-data snapshot by allocating a temporary buffer of 2 MB on the stack inside a tight loop. The default Linux stack size is 8 MB.

**Question:** Is this safe? What is the exact failure mode if you exceed the stack limit? How would you detect this in production before it causes a crash? What is the HFT-correct alternative to avoid this entirely?

> **Hint:** Exceeding the stack causes a SIGSEGV from a guard page at the stack boundary. Detection: set `ulimit -s unlimited` and use `-fstack-usage` (GCC) to see per-function stack usage. The correct alternative: pre-allocate the 2 MB buffer in a thread-local arena at startup, not on the per-call stack.
>
> **Think about:** Why is `alloca(n)` dangerous for large n? What does `alloca` emit in assembly vs a normal stack variable? (It's still `sub rsp, n` — the danger is unbounded n.)

---

### Q6. Heap Non-Determinism in Production (Scenario)

Your market-data handler runs stably for 3 hours, but at random intervals — roughly every 20 seconds — you observe a 500 µs latency spike that you cannot reproduce in testing. `perf stat` shows elevated `minor-faults` during the spike.

**Question:** Walk through your diagnostic process. What glibc malloc behavior could cause periodic 500 µs spikes? What is the tool (`malloc_stats`, `mallopt`, `jemalloc` profiling) you would use and what flag would you set to eliminate this entirely during the trading session?

> **Hint:** Periodic coalescing/trim behaviour: glibc calls `malloc_trim()` internally, which calls `brk()`/`sbrk()` to return memory to the OS. This is a syscall — can take 100s of µs. Fix: `mallopt(M_TRIM_THRESHOLD, -1)` disables trim. Or: switch to `jemalloc` with `MALLOC_CONF=narenas:1,tcache:false` for deterministic behaviour.
>
> **Think about:** How do you verify zero heap allocations on the hot path? Intercept `operator new` with an atomic counter, run the hot path, assert the counter is 0. This is your test harness for production regression.

---

## Section 3 — Smart Pointers: Costs and Trade-offs

---

### Q7. The Atomic Bomb in shared_ptr (Cache Coherence)

You have a multi-core trading system with 40 cores across 2 sockets. A market-data dispatcher passes `shared_ptr<OrderBookSnapshot>` to 20 strategy threads simultaneously (by copying the shared_ptr).

**Question:** What x86 instruction is emitted for each copy? Why does this instruction become catastrophically expensive at 40-core scale? Quantify the total time wasted if the market-data handler fires 1M times/sec. What is the correct replacement design?

> **Hint:** `lock xadd [cache_line], 1` — this broadcasts an RFO (Request For Ownership) to all cores sharing that cache line. 40 cores → 40 RFOs → cache coherence traffic → 40–150 ns per copy. At 1M/sec × 20 threads × 100 ns = 2 ms/sec wasted just on ref-counting.
>
> **Think about:** Replacement options: (1) `intrusive_ptr` — ref count inside the object, single pointer, you control the atomics. (2) Pass by const reference (non-owning). (3) Use an epoch-based reclamation scheme (like RCU) so no ref count is modified on reads. What is the trade-off of each?

---

### Q8. make_shared vs new: The Cache Line Argument (Memory Layout)

Given this code:
```cpp
// Version A:
std::shared_ptr<Order> p1{new Order{id, price}};

// Version B:
auto p2 = std::make_shared<Order>(id, price);
```

**Question (a):** Draw the memory layout for both versions, showing how many heap allocations occur and where the control block sits relative to the `Order` object.

**Question (b):** Version B has a subtle *disadvantage* in systems with long-lived `weak_ptr` references. Explain it precisely — when can it cause a memory *footprint* regression compared to Version A?

> **Hint (a):** Version A: two `malloc` calls → two cache lines (potentially far apart in memory). Version B: one `malloc` → `[use_count|weak_count|deleter|Order]` packed in one allocation, one cache line.
>
> **Hint (b):** With `make_shared`, the control block and the T object share one allocation. The raw memory cannot be freed until `weak_count` drops to zero — even if `use_count` hit zero (i.e., the object is logically dead). If you keep `weak_ptr`s alive for a long time (e.g., observer patterns), the object's bytes remain allocated in memory. With `new Order`, you can free the object immediately when `use_count=0`, and only the control block lingers.

---

### Q9. Designing intrusive_ptr for HFT (Implementation)

**Question:** Implement a minimal `intrusive_ptr<T>` that:
1. Holds a single raw pointer (8 bytes, not 16)
2. Requires T to provide `add_ref()` and `release()` methods
3. Has O(1), ~5 ns copy cost (not 50–150 ns like `shared_ptr`)
4. Works correctly in a single-threaded hot path (no cross-thread sharing)

Why is the copy faster than `shared_ptr`? What atomic ordering would you use for the ref count if you needed thread safety, and why is `memory_order_relaxed` sufficient for increment but NOT for decrement?

> **Hint:** For increment: we only need the count to go up — no other data depends on this. `relaxed` is fine. For decrement: before the destructor runs, we need a `memory_order_acquire` fence to ensure all writes to the object from other threads are visible before we delete it. `shared_ptr` uses `memory_order_acq_rel` on decrement for exactly this reason.

---

## Section 4 — Custom Allocators & PMR

---

### Q10. Monotonic Arena: The Bump Pointer (Implementation)

**Question:** Implement `monotonic_buffer_resource::allocate(size_t bytes, size_t align)` from scratch. Your implementation must:
1. Handle arbitrary alignment (not just power-of-2 sizes)
2. Return `nullptr` (or throw) when capacity is exceeded
3. Contain NO locks, NO syscalls, NO heap calls
4. Cost ~1–3 ns

Then: what does `release()` cost? Why is it O(1) regardless of how many objects were allocated from the arena?

> **Hint:** Alignment trick: `ptr = (current_ + align - 1) & ~(align - 1)` — this rounds up to the next multiple of `align`. Then `current_ = ptr + bytes`. `release()` is just `current_ = buf_start_` — one pointer assignment. It doesn't call any destructors. This is the crucial trade-off: you buy O(1) batch-free at the cost of giving up individual deallocation.
>
> **Think about:** What happens if `T` has a non-trivial destructor and you use arena allocation? You must track destructors separately (as shown in Section 7.2 of the notes — the `Arena` class with `dtors_` vector). When does this tracking itself become a latency hazard?

---

### Q11. Pool Allocator Internals (Design & Complexity)

The `PoolAllocator<T, N>` from the notes uses a union `Slot { T obj; Slot* next; }` to overlay the free-list pointer on top of the object storage.

**Question (a):** Why is a `union` used here instead of a separate bookkeeping array? What does this choice save in terms of memory and cache behaviour?

**Question (b):** What is the pre-condition that must hold for this union trick to be safe? What will happen if `sizeof(T) < sizeof(Slot*)` on a 64-bit system?

**Question (c):** The pool is currently single-threaded. How would you make `allocate()` and `deallocate()` thread-safe with minimal overhead? What is the performance cost of each thread-safety option?

> **Hint (a):** A separate bookkeeping array would double the memory footprint and split hot data across cache lines. The union means free slots *are* the free-list nodes — no extra memory.
>
> **Hint (b):** Pre-condition: `sizeof(T) >= sizeof(void*)` = 8 bytes on x86-64. If T is smaller (e.g., `char`), writing a pointer into a 1-byte slot overwrites adjacent memory → UB/corruption. Fix: `static_assert(sizeof(T) >= sizeof(void*))` in the constructor.
>
> **Hint (c):** Options by cost: (1) `std::mutex` — ~25 ns lock/unlock. (2) `std::atomic<Slot*>` with CAS loop — ~5–15 ns, lock-free but contends under high load. (3) Thread-local pools — ~0 ns contention, each thread has its own pool, but requires cross-thread deallocation strategy.

---

### Q12. Per-Tick Arena Pattern (Production Reasoning)

The notes show this pattern:
```cpp
void on_tick() {
    std::pmr::vector<Order> orders{&tick_arena};
    decode_feed(orders, quotes);
    run_strategy(orders, quotes);
    tick_arena.release();
}
```

**Question:** There is a subtle bug risk in this pattern when `Order` has a non-trivial destructor (e.g., it holds a `std::string` field). Identify the bug. How does the notes' `Arena` class with `dtors_` address this? What is the hidden cost of `dtors_` that the notes warn about?

> **Hint:** `tick_arena.release()` resets the bump pointer but does NOT call destructors. If `Order` owns heap memory (e.g., a `std::string` with SSO overflow), calling `release()` without calling `~Order()` first leaks that heap memory. The `dtors_` vector in Section 7.2 tracks destructor function pointers and calls them in reverse order before reset. The hidden cost: `dtors_` itself is a `std::vector` that can allocate. Use a fixed-capacity `SmallVec<fn_ptr, N>` instead.

---

## Section 5 — Memory Alignment

---

### Q13. Struct Layout and Cache-Line Packing (Design)

Consider this struct used in your order-book's hot path:
```cpp
struct OrderUpdate {
    double   price;     // 8 bytes
    char     side;      // 1 byte  ('B'/'A')
    uint64_t order_id;  // 8 bytes
    char     type;      // 1 byte  ('N'/'M'/'D')
    uint32_t qty;       // 4 bytes
};
```

**Question (a):** Compute `sizeof(OrderUpdate)` using the standard layout rules. How many bytes are wasted as padding?

**Question (b):** Rearrange the fields to minimize `sizeof`. What is the new size?

**Question (c):** You're storing 1 million `OrderUpdate` objects in a flat array for replay. Compare the total memory footprint and L3 cache occupancy of the original vs optimised layout. How does this affect cache miss rate during sequential scan?

> **Hint (a):** `double` at offset 0 (8B), `char` at 8 (1B), 7B padding, `uint64_t` at 16 (8B), `char` at 24 (1B), 3B padding, `uint32_t` at 28 (4B). Total = 32B. Wasted = 10B.
>
> **Hint (b):** Sort by descending alignment: `uint64_t`(8), `double`(8), `uint32_t`(4), `char`(1), `char`(1) → total = 8+8+4+1+1 = 22B + 2B trailing pad = 24B. Or pack to 22B with `#pragma pack(1)` at the cost of unaligned access penalty.
>
> **Hint (c):** 1M × 32B = 32 MB (original) vs 1M × 24B = 24 MB (optimised). L3 cache is 8–64 MB. Smaller footprint → more data fits → fewer cache misses per sequential scan. Quantify: 32 MB / 64B (cache line) = 500K cache misses vs 375K — a 25% reduction in cache traffic.

---

### Q14. Alignment in Custom Allocators (Correctness)

You're writing a bump-pointer allocator. A user allocates:
```cpp
auto* f = arena.allocate<float>(1);   // needs 4-byte alignment
auto* d = arena.allocate<double>(1);  // needs 8-byte alignment
```
The arena buffer starts at address `0x1003` (which is intentionally unaligned).

**Question:** Trace the exact pointer arithmetic for each allocation. What address does each object land at? What goes wrong if you skip alignment rounding for `double`?

> **Hint:** For `float` (align 4): `(0x1003 + 3) & ~3 = 0x1004`. Allocation ends at `0x1004 + 4 = 0x1008`. For `double` (align 8): `(0x1008 + 7) & ~7 = 0x1008`. Already aligned! Allocate at `0x1008`. If you skipped alignment for `double` and placed it at `0x1006`: on x86-64 it works but with a penalty (cache-line split). On ARM it causes SIGBUS.

---

### Q15. SIMD and Alignment Requirements (Performance)

You're vectorising a price array update using AVX2 (256-bit = 32 bytes wide).

**Question:** What alignment is required for `vmovdqa` (aligned load) vs `vmovdqu` (unaligned load)? If your `double price[4]` array starts at an address that is 16-byte aligned but not 32-byte aligned, what is the performance consequence and how do you declare the array to guarantee correct alignment?

> **Hint:** `vmovdqa` requires 32-byte alignment for AVX2. `vmovdqu` accepts any alignment but pays 0–3 extra cycles on modern Intel (Haswell+: penalty is minimal if within cache line; severe if crossing cache line). Fix: `alignas(32) double prices[4]`. Also: when allocating dynamically, use `std::aligned_alloc(32, size)` or `posix_memalign`.

---

## Section 6 — False Sharing & Cache Coherence

---

### Q16. False Sharing Disaster (Diagnosis)

You have a multi-threaded order router with 8 worker threads, each tracking its own count:
```cpp
struct Stats {
    std::atomic<uint64_t> orders_sent[8];
};
```
Benchmarking shows only 3 million increments/sec total across all threads, far below the expected 1+ billion/sec.

**Question (a):** Explain precisely why this structure causes catastrophic performance. Draw the cache-line layout and show which fields land on the same 64-byte line.

**Question (b):** Fix it. Write the corrected struct using `hardware_destructive_interference_size`.

**Question (c):** Why does the fixed version reach ~1.5 billion ops/sec while the broken version only achieves ~3 million? Express the speedup and explain it in terms of cache coherence protocol states (MESI).

> **Hint (a):** `sizeof(atomic<uint64_t>) = 8`. 8 counters × 8B = 64B = exactly one cache line. All 8 counters share one cache line. When Thread 0 writes `orders_sent[0]`, it must acquire the line in M (Modified) state → invalidates the line in all other cores' caches. Thread 1 then writes `orders_sent[1]` → RFO, moves line to M state in core 1 → invalidates core 0. Every increment on any counter causes a global cache invalidation.
>
> **Hint (b):**
> ```cpp
> struct alignas(hardware_destructive_interference_size) PaddedCounter {
>     std::atomic<uint64_t> value;
>     char _pad[hardware_destructive_interference_size - sizeof(atomic<uint64_t>)];
> };
> PaddedCounter counters[8];
> ```
>
> **Hint (c):** Broken: 3M/sec. Fixed: 1.5B/sec. Speedup ≈ 500×. In MESI: broken version keeps each counter's cache line in a perpetual I→S→M→I cycle across cores. Fixed: each counter has its own line, stays in M state on its owning core — no coherence traffic.

---

### Q17. Constructive Interference: Hotpath Struct Packing (Design)

You have an `OrderBook` struct accessed on every tick:
```cpp
struct OrderBook {
    double   best_bid;          // always read on tick
    double   best_ask;          // always read on tick
    uint64_t last_trade_ts;     // always read on tick
    uint64_t book_version;      // always read on tick
    std::array<Level, 10> bids; // only read on deep scan
    std::array<Level, 10> asks; // only read on deep scan
};
```

**Question:** Apply the principle of constructive interference. How would you restructure this for maximum hot-path performance? What does `hardware_constructive_interference_size` tell you, and how do you verify your restructuring works at runtime?

> **Hint:** The hot fields (`best_bid`, `best_ask`, `last_trade_ts`, `book_version`) are 4 × 8B = 32B. These fit within one 64-byte cache line. Move them to the front of the struct (guaranteed by standard layout). The `bids/asks` arrays are only for deep scan — they can sit on different cache lines. Verify: `static_assert(offsetof(OrderBook, bids) >= 64)` ensures the hot fields are isolated. Runtime: use `perf c2c` to check cache-line contention.

---

## Section 7 — Memory Pools and Arenas in Production

---

### Q18. Zero-Malloc Hot Path (Production Architecture)

Design the memory architecture for a market-data decoder that must process 10 million messages/sec with a P99 latency of 500 ns.

**Question:** For each of the following allocations, specify which allocator/strategy to use and justify with latency numbers from the notes:
1. Incoming UDP packet buffer (fixed 1500-byte MTU)
2. Decoded `Order` objects (fixed-size, ~64 bytes each)
3. Per-tick temporary `std::vector<PriceLevel>` for spread calculation
4. Strategy callback function (`std::function` equivalent)
5. The order-book hash map (allocated once at startup)

> **Hint:**
> 1. Stack-local or ring buffer (~0.3 ns) — fixed size, lifespan = duration of decode function
> 2. Pool allocator (~5–10 ns) — fixed size, reusable
> 3. `pmr::monotonic_buffer_resource` arena (~1–3 ns) reset each tick
> 4. `SmallFunction<Sig, 64>` (~0 heap) — SBO avoids heap if capture ≤ 64B
> 5. Pre-allocated at startup with `mmap` + `mlockall` + pre-fault — 0 runtime cost

---

### Q19. Pool Allocator Exhaustion (Resilience)

Your fixed-size `PoolAllocator<Order, 4096>` runs out of free slots during an abnormal market event (flash crash: 10K orders arrive in 1 ms).

**Question:** The current implementation throws `std::bad_alloc`. Propose three alternative exhaustion strategies with their trade-offs. Which would you choose for a live trading system and why?

> **Hint:** Options:
> 1. **Throw `bad_alloc`** — clean but crashes your strategy
> 2. **Fall back to heap** — determinism lost but no crash; mark the allocation as "slow path"
> 3. **Drop/reject the order** — for strategies where missing an order is preferable to latency spikes
> 4. **Pre-allocate 2× expected maximum** + alert on >80% usage — operational discipline
>
> HFT answer: Option 4 is the real answer. You should never hit exhaustion in production. Size the pool for the theoretical maximum burst (exchange max order rate × max latency window), not the average. Alert on 50% utilisation. Treat exhaustion as a bug, not a recoverable error.

---

## Section 8 — Small Buffer Optimization (SBO)

---

### Q20. SSO Detection and std::string Pitfalls (Internals)

**Question (a):** On GCC's libstdc++, what is the SSO threshold for `std::string`? On Clang's libc++? Why do they differ?

**Question (b):** You're storing trading symbol strings like `"AAPL"`, `"MSFT"`, `"BTC-PERPETUAL-USD"`. Which of these triggers a heap allocation? What is the practical fix for the ones that do?

**Question (c):** Write the runtime check from the notes to detect whether a `std::string` is using SSO or heap. Why is this a "hack" rather than a portable API?

> **Hint (a):** GCC: 15 chars (16 with null terminator fits in 16-byte inline buffer). Clang: 22 chars (24-byte `string` has 23 usable inline bytes). They differ due to different sizeof(string) choices (32B vs 24B) and different internal encoding of the length byte.
>
> **Hint (b):** `"AAPL"` (4 chars) → SSO on both. `"MSFT"` (4 chars) → SSO. `"BTC-PERPETUAL-USD"` (17 chars) → SSO on Clang (≤22), heap on GCC (>15). Fix for GCC: store symbols as `std::array<char, 24>` or use a `FixedString<N>` type alias. Never use raw `std::string` for hot-path symbol storage.
>
> **Hint (c):** The check `s.data() >= (char*)&s && s.data() < (char*)&s + sizeof(s)` — this is a hack because the C++ standard doesn't guarantee that SSO stores data inside the object. It's an implementation detail. The portable API would be `s.capacity() <= 15` (GCC) — still implementation-dependent.

---

### Q21. SmallFunction Static Assert (Compile-Time Safety)

You are using `SmallFunction<void(), 64>` for market event callbacks. A new developer writes:
```cpp
std::vector<int> historical_data(10000);
SmallFunction<void(), 64> cb{[historical_data]{ process(historical_data); }};
```

**Question:** What happens at compile time? Why is this the *correct* behaviour for a low-latency system, even though the developer gets a confusing error? How does this differ from `std::function`'s behaviour with the same lambda?

> **Hint:** `sizeof(std::vector<int>)` is typically 24B. But the lambda *captures* the vector by value, so the lambda's size includes the vector's internal pointer + size + capacity (24B). Since 24B < 64B, it actually fits! BUT if the developer captured a `std::array<int, 1000>` (4000B), the static_assert fires at compile time: *"Lambda too large for SmallFunction buffer."*
>
> The correct behaviour: the system tells you at compile time that your callback will cause a heap allocation, forcing you to reconsider the design. `std::function` would silently heap-allocate and cause a latency spike discovered only in production under load. The compile error is a feature, not a bug.

---

### Q22. SmallVec for Order Levels (SBO Design)

Your order-book maintains a list of price levels. In normal markets, a symbol has 5–20 levels. During a flash crash, it can have 200+.

**Question:** Design a `SmallVec<PriceLevel, 20>` strategy that:
1. Uses inline storage for ≤20 elements (zero heap allocation in normal market)
2. Falls back to heap for >20 (correct but slow — acceptable for abnormal events)
3. Can be moved in O(1) regardless of whether inline or heap storage is used

Why is case (3) non-trivial for inline storage?

> **Hint:** For heap storage: move = transfer pointer, O(1). For inline storage: move must memcpy the inline buffer to the destination, O(N). This is why move is NOT O(1) in the inline case — you cannot just transfer a pointer because the data lives inside the object. The correct implementation must check `is_inline()` and branch accordingly. This is the same trade-off that `std::string` makes: SSO strings have O(N) move (in theory), though in practice the branch prediction makes this near-zero cost.

---

## Section 9 — RAII and Resource Management

---

### Q23. Rule of Five Under Exception Safety (Correctness)

Consider this `Buffer` class:
```cpp
class Buffer {
    char* data_;
    size_t size_;
public:
    Buffer(size_t n) : data_{new char[n]}, size_{n} {}
    ~Buffer() { delete[] data_; }
    // Copy constructor MISSING
    // Move constructor MISSING
};
```

**Question (a):** What does the compiler-generated copy constructor do? Why is it wrong for this class?

**Question (b):** The copy assignment operator is also implicitly generated. Write a concrete scenario where this causes a double-free in a trading system that stores `Buffer` objects in a `std::vector<Buffer>`.

**Question (c):** Implement all five special members correctly using the copy-and-swap idiom.

> **Hint (a):** Compiler generates a memberwise copy: `data_ = other.data_` — a shallow copy. Both objects now point to the same heap buffer. When either destructs, `delete[] data_` frees the buffer. The other now holds a dangling pointer → undefined behaviour on next access or double-free on its destruction.
>
> **Hint (b):** `vec.push_back(b)` — if `push_back` triggers a reallocation, it copies all existing `Buffer` objects. If the copy constructor does a shallow copy, both the original in the old buffer and the new copy own `data_`. When the old buffer is freed, all originals are destroyed → their destructors call `delete[] data_` → but the new copies still hold those pointers → use-after-free.

---

### Q24. RAII for HFT Resources (Production Patterns)

You need to write RAII wrappers for three resources used in your trading system:
1. A UDP multicast socket file descriptor
2. A `mlockall()` memory pin (to prevent page faults)
3. A CPU affinity setting (to pin thread to core 0)

**Question:** For each, implement the RAII class with constructor, destructor, and correct move semantics. Which of the three should disable copy? Why?

> **Hint:** All three should disable copy:
> - Socket FD: copying a file descriptor creates a duplicate handle that will be closed twice
> - `mlockall`: it's process-wide, not copyable by nature
> - CPU affinity: a thread affinity setting belongs to exactly one thread
>
> Move semantics: Socket FD → sentinel value is `-1`. Memory lock → `bool locked_` flag. CPU affinity → `bool set_` flag. On move, transfer ownership and set source to "null" state so the source destructor is a no-op.

---

### Q25. ScopedTimer with __rdtsc (Performance Instrumentation)

The notes show a `ScopedTimer` using `__rdtsc()`. You want to measure the latency of your decode + strategy pipeline with nanosecond resolution.

**Question (a):** Why use `__rdtsc()` over `std::chrono::high_resolution_clock::now()`? What is the specific overhead difference?

**Question (b):** `__rdtsc()` returns CPU cycles. How do you convert to nanoseconds? What are two pitfalls of this conversion that can give you wrong latency numbers?

**Question (c):** Your latency histogram shows occasional outliers at 100× the median. How do you distinguish a genuine latency spike from a `__rdtsc` measurement artifact?

> **Hint (a):** `__rdtsc` is a single `RDTSC` instruction → ~1–5 cycles. `std::chrono::high_resolution_clock::now()` typically calls `clock_gettime(CLOCK_MONOTONIC)` → a vDSO call → ~20–40 ns. For measuring sub-100 ns events, the timer overhead matters.
>
> **Hint (b):** Conversion: `ns = cycles / (CPU_GHz)`. Pitfall 1: CPU frequency scaling (turbo boost, power saving) — the TSC may not match actual GHz. Fix: use `CLOCK_TAI` calibration or pin the CPU to a fixed frequency. Pitfall 2: `RDTSC` is not serialising — the CPU can reorder it with surrounding instructions. Use `RDTSCP` or `LFENCE; RDTSC` for precise measurement boundaries.
>
> **Hint (c):** Real spike: correlates with OS scheduler preemption, NIC interrupt, or NUMA traffic. TSC artifact: `elapsed_cycles` wraps (impossible on 64-bit for reasonable measurements) or shows < 0 (RDTSC executed out of order). Mitigation: add `if (elapsed > 1e9) log_outlier_with_stack_trace()` and use `isolcpus` kernel boot parameter to prevent scheduler interference.

---

## Section 10 — Full System Design

---

### Q26. Zero-Allocation Pipeline Architecture (System Design)

The notes conclude with the "Zero-Allocation Market Data Pipeline" pattern. Reconstruct it from first principles:

**Question:** You are building the memory architecture for a FIX protocol decoder that must achieve:
- < 500 ns P99 for message decode
- Zero heap allocations on the hot path
- Support for 100,000 simultaneous open orders
- 8 cores, NUMA-aware

Describe the complete startup sequence and hot-path memory flow. For each memory region, state: allocator type, size, allocation time (startup vs hot path), and the `latency cost per operation`.

> **Hint — Startup sequence:**
> 1. `mlockall(MCL_CURRENT | MCL_FUTURE)` — prevent all future page faults
> 2. `PoolAllocator<Order, 100000>` — pre-allocate order pool, pre-fault with memset
> 3. Thread-local `monotonic_buffer_resource` (1 MB per thread × 8 threads = 8 MB) — pre-fault
> 4. NUMA-bind all memory to the correct socket for each thread's core
> 5. `SmallFunction<void(const Order&), 64>` strategy callbacks — register at startup, never reallocate
>
> **Hot path (per message):**
> - Receive packet: ring buffer pop (~1 ns)
> - Decode into arena: bump pointer (~1–3 ns)
> - Allocate Order: pool allocator pop (~5–10 ns)
> - Execute strategy callback: `SmallFunction` invoke (~3–5 ns including indirect call)
> - Reset arena: pointer reset (~0.3 ns)
> - Total memory overhead per message: ~10–20 ns — well within 500 ns budget

---

### Q27. Allocator Selection Decision Tree (Applied)

**Question:** For each scenario below, select the correct allocator from: {stack, monotonic arena, pool allocator, tcmalloc, glibc malloc} and justify:

| Scenario | Your Choice | Latency Budget |
|---|---|---|
| Temporary decode buffer, 200 bytes, dies at end of function | ? | < 1 ns |
| 10,000 fixed-size `Order` objects, randomly created/destroyed | ? | < 10 ns |
| Per-tick batch of variable-size message structs, all freed together | ? | < 5 ns |
| One-time allocation of a 500 MB order history log | ? | No budget (startup) |
| Off-hot-path logging strings, unpredictable size | ? | < 200 ns |

> **Answers:** Stack / Pool allocator / Monotonic arena / glibc malloc or mmap / tcmalloc.
> The exercise: reason about *why* each is correct — not just which. The key axis is: (lifetime pattern) × (size variability) × (thread sharing) → determines allocator class.

---

### Q28. Final Boss — Debug the Memory Bug (Integration)

A colleague writes this HFT order router:
```cpp
std::vector<std::shared_ptr<Order>> hot_orders;

void on_market_data(const Feed& feed) {
    for (auto& msg : feed.messages) {
        auto order = std::make_shared<Order>(msg.id, msg.price, msg.qty);
        hot_orders.push_back(order);           // Line A
        execute_strategy(order);               // Line B
    }
}

void execute_strategy(std::shared_ptr<Order> order) {  // Line C
    // ... strategy logic
    if (order->is_filled()) {
        // notify observers
        for (auto& obs : observers_)
            obs->on_fill(order);               // Line D
    }
}
```

**Question:** Identify ALL memory/latency anti-patterns in this code (there are at least 6). For each, state: what it is, what the latency cost is, and the fix.

> **Full Answer Key:**
> 1. **Line A — `push_back` on `vector<shared_ptr>`**: `push_back` may reallocate the vector → heap allocation on hot path. Fix: `hot_orders.reserve(MAX_ORDERS)` at startup.
> 2. **Line A — storing `shared_ptr` in a `vector`**: `hot_orders` growing unboundedly → memory leak if orders are never removed. Fix: use an intrusive linked list or an index into a pool.
> 3. **`make_shared` on hot path**: heap allocation on every market data message. Fix: allocate from pool allocator, use raw pointer or intrusive_ptr.
> 4. **Line C — `shared_ptr` passed by value**: copies the shared_ptr → `lock xadd` (atomic increment) → 50–150 ns per call. Fix: pass by `const shared_ptr&` or redesign to avoid shared ownership entirely.
> 5. **Line D — `obs->on_fill(order)` with `shared_ptr` copy into `on_fill`**: another atomic increment per observer. Fix: pass `const Order&` (non-owning reference) if observers don't need ownership.
> 6. **No arena/pool discipline**: entire hot path calls heap on every message. Fix: pre-allocate everything, use pool for `Order`, arena for decode temporaries, SmallFunction for callbacks.

---

## Quick Reference — Latency Cheat Sheet

| Operation | Latency | Notes |
|---|---|---|
| Stack alloc (sub rsp, N) | ~0.3 ns | One instruction |
| Bump-pointer arena alloc | ~1–3 ns | Pointer arithmetic only |
| Pool allocator alloc | ~5–10 ns | Two pointer ops |
| tcmalloc hot path | ~15 ns | Thread-local cache |
| glibc malloc | ~50 ns | Bin search + lock |
| unique_ptr move | ~0.3 ns | 3 instructions |
| shared_ptr copy | ~50–150 ns | `lock xadd` + MESI RFO |
| L1 cache access | ~1 ns | 4 cycles |
| L2 cache access | ~4 ns | 12 cycles |
| L3 cache access | ~15 ns | 40 cycles |
| DRAM access (local) | ~70 ns | 200 cycles |
| DRAM access (NUMA remote) | ~140 ns | 400 cycles |
| Kernel page fault | ~1–10 µs | First touch |
| False-shared counter | ~2–5 M/s | ← ELIMINATE |
| Padded counter | ~1.5 B/s | Per-line, correct |

---
*Generated from 08_memory_management_deep_dive.md · cpp-high-performance · Low-Latency C++ track*
