# Deep Dive — Data Structures (HFT-grade internals)

> Companion to [`NOTES.md`](./NOTES.md). This document goes to the **memory-layout level** of every container that gets asked in HFT and quant-dev interviews, then maps to the production patterns (SPSC ring buffer, order books, flat hash maps).

---

## Table of Contents
1. [`std::vector` — the 3-pointer flat array](#vector)
2. [`std::array` — stack-allocated, compile-time](#array)
3. [`std::deque` — chunked map-of-blocks](#deque)
4. [`std::list` / `std::forward_list` — why HFT (mostly) avoids them](#list)
5. [`std::map` / `std::set` — red-black tree internals](#map)
6. [`std::unordered_map` — separate chaining, mandated by the standard](#umap)
7. [`absl::flat_hash_map` / robin_hood — open addressing](#flat)
8. [Container adaptors — stack, queue, priority_queue](#adaptors)
9. [AoS vs SoA — when memory layout decides performance](#soa)
10. [Cache lines & false sharing — `alignas(64)`](#cache)
11. [Lock-free SPSC ring buffer — the hot-path queue](#spsc)
12. [Order-book data structures — the case study](#book)
13. [Allocators & memory pools — preview of Chapter 7](#alloc)
14. [Interview drill — 25 questions HFT firms actually ask](#drill)

---

<a id="vector"></a>
## 1. `std::vector` — the 3-pointer flat array

### Memory layout

A `std::vector<T>` object is **24 bytes on a 64-bit system** (three raw pointers, sometimes plus an empty-base-optimized allocator):

```
        +────────────────+
vector  │  begin_       ─┼─►  ┌─────┬─────┬─────┬─────┬─────┬─────┐
        │  end_         ─┼────────────────^                       │
        │  end_of_cap_  ─┼──────────────────────────────────────^ │
        +────────────────+         T[0]   T[1]   ...   T[size-1]   <end_of_cap>
                                      contiguous heap buffer
```

- `size() = end_ − begin_`
- `capacity() = end_of_cap_ − begin_`
- The buffer is **one contiguous allocation** from `operator new`/the allocator.
- Iterating the vector = bumping a pointer = the hardware prefetcher prefetches the next cache line for free.

### Growth factor — interview gold

| STL              | Growth factor | Why                                                          |
|------------------|---------------|--------------------------------------------------------------|
| libstdc++ (gcc)  | **2.0**       | strict amortized O(1) proof, easy to reason about            |
| libc++ (clang)   | **2.0**       | same                                                         |
| MSVC (Microsoft) | **1.5**       | reusable freed pages → lower steady-state heap fragmentation |

**Mathematical proof of amortized O(1):**

Starting from capacity 1, doing N pushes triggers reallocations at capacities 1, 2, 4, …, N. Total work to copy elements during reallocation:

```
1 + 2 + 4 + … + N  =  2N − 1  =  O(N)
```

So N pushes = O(N) total work → **O(1) amortized** per push.

> Counterexample question: "Why doesn't growth factor < 2 break amortized O(1)?" Answer: any factor `r > 1` works; the sum is geometric: `N + N/r + N/r² + … = N · r/(r-1)` which is still O(N).

### `push_back` step-by-step

```cpp
template<class T> void vector<T>::push_back(T&& v) {
    if (end_ == end_of_cap_) {           // capacity exhausted
        size_t old_cap = capacity();
        size_t new_cap = old_cap ? 2 * old_cap : 1;
        T* new_buf = alloc_.allocate(new_cap);

        // CRITICAL: move-construct only if T's move ctor is noexcept;
        // otherwise copy-construct, to preserve the strong exception guarantee.
        if constexpr (std::is_nothrow_move_constructible_v<T>)
            uninitialized_move(begin_, end_, new_buf);
        else
            uninitialized_copy(begin_, end_, new_buf);

        destroy(begin_, end_);
        alloc_.deallocate(begin_, old_cap);

        begin_       = new_buf;
        end_         = new_buf + old_cap;
        end_of_cap_  = new_buf + new_cap;
    }
    new (end_) T(std::move(v));          // placement new
    ++end_;
}
```

**HFT lesson:** mark your types' move constructors `noexcept`. Without it, vector copies on resize — death by 1000 copies.

### Iterator invalidation

| Operation                                       | What invalidates                            |
|-------------------------------------------------|---------------------------------------------|
| `push_back` (no realloc)                        | `end()` only                                |
| `push_back` (realloc)                           | **everything** (begin, end, all iterators)  |
| `insert` at position p (no realloc)             | everything from p to end                    |
| `erase(p)`                                      | p and everything after                      |
| `clear()`                                       | everything                                  |
| `reserve(n)` with n > capacity                  | everything                                  |
| `reserve(n)` with n ≤ capacity                  | nothing                                     |

### When `std::vector` is the wrong tool
- You need stable references to elements **across inserts** → use `std::list` or `std::deque` (or, better, `std::vector<std::unique_ptr<T>>`).
- You need O(1) push-front → use `std::deque` or a ring buffer.
- You need to remove the middle element a lot → consider a free-list flat container.

---

<a id="array"></a>
## 2. `std::array<T,N>` — stack-allocated, size known at compile time

```
sizeof(std::array<int, 4>) == 16   // exactly sizeof(int)*4 — no overhead
```

- Lives **on the stack** (or in static storage / a containing class).
- Zero heap allocations.
- `constexpr`-friendly (can be filled at compile time).
- Used in HFT for fixed-length packets, fixed-size buffers, `O(1)` mailboxes.

```cpp
constexpr std::array<int, 5> primes{2, 3, 5, 7, 11};   // baked into the binary
```

---

<a id="deque"></a>
## 3. `std::deque<T>` — chunked map-of-blocks

```
              ┌────────────────────────────┐
       map ──►│  blk*  blk*  blk*  blk*    │   (pointer table, "the map")
              └──┬─────┬─────┬─────┬───────┘
                 ▼     ▼     ▼     ▼
              [...]  [...] [...]  [...]         (each chunk is T[chunk_size])
```

- Chunk size is implementation-specific:
  - **libstdc++** (gcc): 512 / sizeof(T) elements per chunk (min 1)
  - **libc++** (clang): max(4096 / sizeof(T), 16) elements per chunk
  - **MSVC**: 16 / sizeof(T) (the worst — many small chunks → many cache misses)
- `sizeof(std::deque<int>)` on libstdc++ ≈ **80 B** (the map pointer, 4 iterators, a chunk count).
- `push_front`/`push_back` are O(1) (amortized — they grow the map when needed).
- Iteration crosses chunk boundaries → **branches inside the iterator**, cache-unfriendly.
- **Pointer/reference stability:** elements **do not move** when you push/pop at the ends. Only the *iterators* may invalidate.

**HFT verdict:** rarely the right pick. If you want O(1) double-ended push/pop, you almost certainly want a fixed-capacity ring buffer instead.

---

<a id="list"></a>
## 4. `std::list<T>` and `std::forward_list<T>` — linked nodes

```
list<T>  head ─► [prev|val|next] ⇄ [prev|val|next] ⇄ [prev|val|next] ─► nullptr
```

Each node = one heap allocation. On a 64-bit system: 2 pointers (16 B) + alignment-padded `T`.

- O(1) `splice` (re-link without moving elements) — **the one true use case**.
- O(1) erase given an iterator.
- Iteration = **pointer chase** = a cache miss per element if nodes aren't allocated contiguously.

In HFT we use **intrusive doubly-linked lists** instead (the prev/next pointers live inside the `T` itself, not in a separate node). This eliminates the allocator overhead and lets one element belong to multiple lists at once.

---

<a id="map"></a>
## 5. `std::map<K,V>` / `std::set<K>` — red-black tree

### Node layout

```
struct rb_node {
    rb_node* parent;
    rb_node* left;
    rb_node* right;
    enum Color { red, black } color;   // 1 bit, but usually padded to 8 B
    std::pair<const K, V> data;
};
```

Per-node overhead on x86-64: **3 × 8 B pointers + 1 B color + padding ≈ 32 B**. Each node is a separate heap allocation.

### Invariants (red-black tree)
1. Every node is red or black.
2. The root is black.
3. Every leaf (NIL) is black.
4. A red node's children are black (no two reds in a row on a path).
5. Every root-to-leaf path has the same number of black nodes ("black height").

Consequence: height ≤ 2·log₂(n+1) → all ops O(log n).

### Why `std::map` is slow in practice

Each tree descent loads one node (~32 B + key+value) into L1, makes one comparison, then jumps to a child node living **somewhere else** in the heap. So a lookup in a 1M-element `std::map` is roughly **20 cache misses** (log₂(1M) ≈ 20). Compared to a sorted-vector binary search that touches ~6 cache lines (1M/16 elements per cache line × 6 levels deep), the vector wins despite both being "O(log n)".

### `boost::container::flat_map` and `absl::btree_map`
- **`boost::flat_map`** = sorted `std::vector<pair<K,V>>` + binary search. Lookups are super fast (binary search on contiguous memory). Inserts in the middle are O(n) (shift). **Best when reads ≫ writes.**
- **`absl::btree_map`** = real B-tree. Internal nodes pack many keys (typically 32-256 keys per node). One cache-line load gives you ~16-32 comparisons. **2-5× faster than std::map** for the same operations.

### Iterator stability — the one thing `std::map` does better than hash maps
Inserting or erasing any key in a `std::map` does **not** invalidate iterators to other keys. This is why `std::map` is used in HFT for things like "ordered set of price levels where I'm holding pointers to specific levels".

---

<a id="umap"></a>
## 6. `std::unordered_map<K,V>` — separate chaining (mandated)

### Layout

```
buckets:   [▼] [_] [▼] [▼] [_] [_] [▼] [_] [_] [▼] ...
            │       │   │           │           │
            ▼       ▼   ▼           ▼           ▼
           node    node node       node        node
            │       │   │           │           │
            ▼       ▼   ▼           ▼           ▼
            -       -   -          node          -
```

- `buckets` is a `vector<node*>`.
- Each node is heap-allocated and stores `{ K, V, next }` plus possibly a cached hash.
- A lookup is: `hash(k) → index → bucket → walk the linked list with ==`.

### Why the standard *forces* this layout

The standard requires:
1. **Iterator stability** under insert (open addressing rehashes move elements → can't promise this).
2. **Pointer/reference stability** to elements as long as no rehash.
3. The public **bucket API** — `bucket(k)`, `bucket_size(n)`, `begin(n)`, `end(n)`.

These together rule out open addressing. So `std::unordered_map` is forever stuck with separate chaining → forever slower than `absl::flat_hash_map`.

### Load factor & rehash

```cpp
um.bucket_count();           // current number of buckets
um.size();                   // current number of elements
um.load_factor();            // size / bucket_count
um.max_load_factor();        // default 1.0
um.max_load_factor(0.5f);    // half the load_factor → fewer collisions, more memory
um.rehash(n);                // make bucket_count ≥ n
um.reserve(n);               // rehash(ceil(n / max_load_factor)) — call this!
```

A rehash:
1. Allocates a new bucket array (usually next prime ≥ 2× current).
2. Iterates every node, computes `hash(k) % new_bucket_count`, re-links into a new bucket.
3. Frees the old bucket array.

That single insert is O(n). Across n inserts it amortizes to O(1) per insert.

### Cost of a lookup (cache-cold)

```
1. Hash compute:                    ~5-20 ns (good hash; bad hash can be 100+)
2. Load bucket head pointer:        ~50-100 ns (L3 miss possible)
3. Load first node:                 ~50-100 ns (L3 miss likely; node lives elsewhere)
4. Key compare:                     ~1-5 ns
5. If collision: load next node:    +50-100 ns per probe
```

Worst case 2 cache misses for a hit, more on collision. This is what makes `flat_hash_map` **2-5× faster** in real HFT workloads.

### Custom hash — `unordered_map<OrderId, Order>`

```cpp
struct OrderId { uint64_t client_id; uint32_t local_id; };

struct OrderIdHash {
    size_t operator()(const OrderId& o) const noexcept {
        // Mix client and local — splitmix64-style finalizer
        uint64_t x = o.client_id ^ (uint64_t(o.local_id) << 32);
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
        x =  x ^ (x >> 31);
        return size_t(x);
    }
};

struct OrderIdEq {
    bool operator()(const OrderId& a, const OrderId& b) const noexcept {
        return a.client_id == b.client_id && a.local_id == b.local_id;
    }
};

std::unordered_map<OrderId, Order, OrderIdHash, OrderIdEq> book;
```

> **Interview trap:** if your hash function is `return 0;` (or anything constant), every key goes into the same bucket → lookups degrade to O(n). The interviewer will laugh.

---

<a id="flat"></a>
## 7. Open-addressing maps — `absl::flat_hash_map`, `robin_hood`, `boost::unordered_flat_map`

### Layout (one giant flat array)

```
  metadata: [_][H][T][_][_][H][_][T][_][_]...  (1 byte per slot: empty / sentinel / hash-bits)
  slots:    [_][k,v][k,v][_][_][k,v][_][k,v]...
```

- One element per slot.
- On collision, probe the next slot (linear) or use Robin Hood / quadratic probing.
- Metadata array stores 1 byte per slot (often the top 7 bits of the hash), so the SIMD probe can compare **16 slots' metadata at once** (SSE2 PCMPEQB) → near-constant lookups.

### Why this is so much faster than separate chaining

- One cache line touched per lookup (the bucket and the slot live next to each other).
- SIMD probes 16 candidates at once.
- No pointer chasing.
- No per-node allocation → 50-80% lower memory.

### The trade-off

- **No iterator stability across insert** — rehash moves everything. Don't hold an iterator across an insert.
- **No bucket API** — but you almost never need it.

For HFT lookups on the hot path (e.g. "find the order by order ID to cancel it"), `absl::flat_hash_map` is the standard answer.

---

<a id="adaptors"></a>
## 8. Container adaptors — stack, queue, priority_queue

```cpp
template<class T, class Container = std::deque<T>>          class stack;
template<class T, class Container = std::deque<T>>          class queue;
template<class T, class Container = std::vector<T>,
         class Compare  = std::less<typename Container::value_type>>
                                                            class priority_queue;
```

- **`std::stack`** with default deque backend ≈ 80 B object + chunk allocations. Use `std::stack<T, std::vector<T>>` for performance.
- **`std::queue`** needs `pop_front`, which vector lacks → backend has to be deque or list.
- **`std::priority_queue`** uses a binary heap inside the vector via `std::push_heap`/`std::pop_heap`.

### Binary heap layout in a vector

```
  index:   0    1    2    3    4    5    6
  vals:  [50] [30] [40] [10] [20] [15] [25]

         parent(i) = (i-1)/2
         left(i)   = 2i+1
         right(i)  = 2i+2
```

`push_heap`: place at index `n`, **sift up** swapping with parent while `parent < new`.
`pop_heap`: swap index 0 with index `n-1`, decrement n, **sift down** at index 0 while > children.

Both are O(log n) and use only sequential vector access → very cache-friendly. This is why `priority_queue<T, vector<T>>` is the default — and the right default.

---

<a id="soa"></a>
## 9. AoS vs SoA — let the access pattern dictate the layout

```cpp
// AoS — natural OO style
struct Order {
    double   price;
    int      qty;
    uint64_t id;
    bool     side;
};
std::vector<Order> ord_aos;

// SoA — natural performance style
struct OrdersSoA {
    std::vector<double>   prices;
    std::vector<int>      qtys;
    std::vector<uint64_t> ids;
    std::vector<bool>     sides;
};
```

### Cache-line accounting (1 M orders, sum the prices)

| Layout | Bytes touched per element | Useful bytes | Waste | Cache lines per 1M ops |
|--------|---------------------------|--------------|-------|------------------------|
| AoS    | 24 (struct, padded)       | 8 (`price`)  | 67%   | ~375 000               |
| SoA    | 8 (just price)            | 8            | 0%    | ~125 000               |

So SoA touches **3× fewer cache lines**, and the SIMD vectorizer can `vaddpd` 4 doubles per cycle on AVX → another 4×. Real-world speedup: **5-20×** on a hot price-sum loop.

### When AoS is the right answer

If your hot loop reads **every field** of an object (e.g. serializing every order to a FIX message), AoS has all 24 B on the same cache line → fewer cache lines pulled overall.

### The 80% rule

If your hot loop reads ≥ 80% of the bytes of a struct, AoS is fine. Below 80%, switch to SoA — the cache waste dominates.

---

<a id="cache"></a>
## 10. Cache lines and false sharing — `alignas(64)`

### Cache line basics
- 64 B on x86, 128 B on Apple Silicon (M-series), some ARM cores have 64 B.
- The MESI coherence protocol invalidates a line in *every* other core's cache when one core writes.
- A whole cache line of 64 B is the smallest unit you can write **atomically** to RAM.

### False sharing

```cpp
struct BadCounters {                 // 16 B → both atomics on the SAME line
    std::atomic<uint64_t> producer;
    std::atomic<uint64_t> consumer;
};                                    // ⇒ thread A writes producer → invalidates
                                      //   thread B's cached consumer → cache thrash
```

```cpp
struct GoodCounters {
    alignas(64) std::atomic<uint64_t> producer;
    alignas(64) std::atomic<uint64_t> consumer;
};                                    // ⇒ each lives in its own cache line
```

### `std::hardware_destructive_interference_size` (C++17)

```cpp
#include <new>
struct GoodCountersPortable {
    alignas(std::hardware_destructive_interference_size)
        std::atomic<uint64_t> producer;
    alignas(std::hardware_destructive_interference_size)
        std::atomic<uint64_t> consumer;
};
```

Beware: on Apple Silicon `hardware_destructive_interference_size == 128`, so code padded to 64 B will still false-share. Always benchmark.

---

<a id="spsc"></a>
## 11. Lock-free SPSC ring buffer — the HFT hot-path queue

### Design — power-of-two size + `head/tail` indices

```
indices wrap modulo capacity (power of two so & (N-1) == % N)

  head_  ─►  ┌───┬───┬───┬───┬───┬───┬───┬───┐
             │ x │ x │ x │ x │ . │ . │ . │ . │   buffer[N]   (N = 8 here)
  tail_  ─►  └───┴───┴───┴───┴───┴───┴───┴───┘
   producer writes at head_, consumer reads at tail_
```

```cpp
template<class T, size_t N>
class spsc_ring {
    static_assert((N & (N-1)) == 0, "N must be power of two");

    alignas(64) std::atomic<size_t> head_{0};   // producer writes
    alignas(64) std::atomic<size_t> tail_{0};   // consumer reads
    alignas(64) std::array<T, N>    buf_;

public:
    // PRODUCER side — only one producer thread
    bool push(const T& v) noexcept {
        const auto h = head_.load(std::memory_order_relaxed);          // I own head
        const auto t = tail_.load(std::memory_order_acquire);           // see consumer's reads
        if (h - t == N) return false;                                   // full
        buf_[h & (N-1)] = v;
        head_.store(h + 1, std::memory_order_release);                  // publish
        return true;
    }

    // CONSUMER side — only one consumer thread
    bool pop(T& v) noexcept {
        const auto t = tail_.load(std::memory_order_relaxed);           // I own tail
        const auto h = head_.load(std::memory_order_acquire);           // see producer's writes
        if (h == t) return false;                                       // empty
        v = buf_[t & (N-1)];
        tail_.store(t + 1, std::memory_order_release);                  // publish
        return true;
    }
};
```

### Why each memory ordering?

| Operation                | Order              | Reason                                                                  |
|--------------------------|--------------------|-------------------------------------------------------------------------|
| Producer reads its head  | `relaxed`          | only one writer (this thread), no synchronization needed                |
| Producer reads tail      | `acquire`          | must see the consumer's updates to tail before checking "full"          |
| Producer writes head     | `release`          | publish the slot's contents before the consumer can see head increment   |
| Consumer reads its tail  | `relaxed`          | only one reader (this thread), no synchronization needed                |
| Consumer reads head      | `acquire`          | must see the producer's published writes before reading the slot         |
| Consumer writes tail     | `release`          | publish that the slot is now free for the producer                      |

> `seq_cst` would also work — but it's the strongest, slowest barrier (full fence on x86, expensive on ARM). Acquire/release is the **minimum sufficient** ordering for SPSC. This is exactly the kind of nuance HFT interviewers probe.

### Cache-line discipline

- `head_` on its own cache line, `tail_` on its own cache line → no false sharing between producer and consumer.
- The buffer of T's also on its own cache line for the same reason (and aligned for SIMD if `T` is numeric).

---

<a id="book"></a>
## 12. Order-book data structures — the HFT case study

### What is an order book?

A limit-order book holds outstanding **limit orders** organized by price. Two sides: **bids** (buy orders, want low prices) and **asks** (sell orders, want high prices). At each price there's a **FIFO queue** of orders (price-time priority).

```
              ASKS (sell side)               BIDS (buy side)
              price       qty                 price       qty
              ─────────────                   ─────────────
              101.5       100                 100.4       50    ← best bid
              101.6       200                 100.2       250
              101.7        20                 100.1        80
                                              99.5         10
```

### Design 1 — textbook: `std::map<Price, Level>`

```cpp
struct Level { uint64_t qty; std::list<Order> orders; };

std::map<int, Level, std::greater<int>> bids_;   // descending — best at begin()
std::map<int, Level>                    asks_;   // ascending  — best at begin()
```

- `best_bid()` = `bids_.begin()->first` — O(1) since `begin()` is cached.
- `add_order`, `cancel_order`, `match`: O(log n).
- Iterator stability survives all inserts/erases → safe to hold pointers.

**Why this is too slow for HFT:** every tree descent is ~20 cache misses; production books receive millions of messages per second; the red-black tree spends its life thrashing the cache.

### Design 2 — sorted `std::vector<Level>` + binary search

```cpp
struct Level { int price; uint64_t qty; /* orders */ };
std::vector<Level> bids_;   // sorted descending
```

- `best_bid()` = `bids_.front()` — O(1).
- `lower_bound`/`upper_bound` over a contiguous array — ~6 cache misses for 1M levels.
- Insert in the middle = O(n) shift. Bad for active books, but for **top-of-book-only** flows this is fine.

### Design 3 — **price-indexed array** (the production pattern)

If the price range is bounded (e.g. tick 0 .. MAX_TICKS = 1M), just use an array:

```cpp
struct Level { uint64_t qty; IntrusiveList<Order> orders; };
Level levels_[MAX_TICKS];          // sparse, but O(1) per level
int best_bid_tick_;                 // tracked as you add/remove
int best_ask_tick_;
```

- `add_order(price_tick, qty)` is O(1): index into the array.
- `cancel_order(order_ptr)` is O(1) given an intrusive iterator.
- `match`: pop best bid/ask in O(1), then bump `best_bid_tick_`/`best_ask_tick_` until you find a non-empty level (amortized O(1) per match in practice).

This is the layout every major HFT shop ends up with.

### Cancellations — intrusive doubly-linked list of orders

```cpp
struct Order {
    Order* prev_;       // intrusive
    Order* next_;
    Level* level_;      // back-pointer
    uint64_t id;
    uint64_t qty;
};
```

The Level holds head/tail pointers. Cancel is `unlink(this)` — O(1) without searching. Lookup-by-id uses a `flat_hash_map<OrderId, Order*>` on the side.

---

<a id="alloc"></a>
## 13. Allocators & memory pools

Every STL container has an allocator template parameter:

```cpp
template<class T, class Alloc = std::allocator<T>>
class vector;
```

For HFT you want:
- **No `operator new` on the hot path** — every `malloc` is a potentially long, non-deterministic call into the system allocator (lock contention, page faults).
- **Bounded memory** — known size, lives in pre-allocated pools.

### `std::pmr` (polymorphic memory resources) — C++17

```cpp
#include <memory_resource>
std::array<std::byte, 1 << 20> arena;                       // 1 MB stack arena
std::pmr::monotonic_buffer_resource mbr{arena.data(), arena.size()};
std::pmr::unordered_map<int, int>   m(&mbr);                // never touches the heap
```

Zero heap allocations until the arena is exhausted. Great for short-lived containers in the hot path.

### Pool allocator pattern

```cpp
template<class T, size_t N>
class PoolAllocator {
    union Slot { T value; Slot* next; };
    alignas(T) std::byte storage_[N * sizeof(Slot)];
    Slot* free_list_;

public:
    PoolAllocator() {
        free_list_ = reinterpret_cast<Slot*>(storage_);
        for (size_t i = 0; i < N - 1; ++i)
            free_list_[i].next = &free_list_[i+1];
        free_list_[N-1].next = nullptr;
    }
    T* allocate() {
        if (!free_list_) throw std::bad_alloc{};
        Slot* s = free_list_;
        free_list_ = s->next;
        return reinterpret_cast<T*>(s);
    }
    void deallocate(T* p) {
        auto* s = reinterpret_cast<Slot*>(p);
        s->next = free_list_;
        free_list_ = s;
    }
};
```

- `allocate`/`deallocate` are O(1), branch-free, no syscall.
- All memory is one contiguous block → great cache behavior.

---

<a id="drill"></a>
## 14. Interview drill — 25 questions HFT firms actually ask

1. What is `sizeof(std::vector<int>)` on a 64-bit system? Why?
2. What growth factor does gcc's `std::vector` use? Why not 1.5? Why not the golden ratio?
3. Walk me through what happens in memory when `vector::push_back` triggers a realloc.
4. Why must your `T`'s move constructor be `noexcept` for vector to move on resize?
5. Which container operations invalidate iterators in `vector`? In `map`? In `unordered_map`?
6. Why is `std::list` almost always the wrong answer in modern C++?
7. Explain the chunked layout of `std::deque`. Why is iteration slower than `std::vector`?
8. Why is `std::map` implemented as a red-black tree and not an AVL tree? (Insertion/erasure does fewer rotations; mean rotations per op is lower.)
9. What invariants does a red-black tree maintain? Why do they give O(log n)?
10. Stepanov said B-trees are "strictly better" than red-black trees for `std::map`. Why?
11. Why does the C++ standard effectively mandate **separate chaining** for `std::unordered_map`?
12. Show me the memory layout of `std::unordered_map`. How many cache lines does a lookup touch?
13. Why is `absl::flat_hash_map` 2-5× faster than `std::unordered_map`?
14. What does `unordered_map::reserve(N)` actually do?
15. What is the default `max_load_factor`? When would you lower it? When would you raise it?
16. What is **false sharing**? Show me how you'd fix it with `alignas`.
17. What is `std::hardware_destructive_interference_size`? What is it on Apple Silicon?
18. Write an SPSC ring buffer. What memory orderings do producer and consumer need?
19. Why is `memory_order_release` on the producer's head store enough? Why not `seq_cst`?
20. Why is `priority_queue` `vector`-backed by default?
21. Show me the implicit array layout of a binary heap. What are the index formulas?
22. AoS vs SoA: when does each win? What's the 80% rule?
23. Design an order book in C++. What data structure for the price levels? Why?
24. Why is an array-indexed order book (one slot per integer price tick) usually faster than `std::map`?
25. Why would you write a custom allocator for an STL container in a trading system?

---

## Further reading

- Bannalia / Joaquín M López Muñoz, *"Advancing the State of the Art for std::unordered_map Implementations"* — the definitive critique of separate chaining in the standard.
- Martin Ankerl, *"Comprehensive C++ Hashmap Benchmarks"* — Robin Hood and friends vs `std::unordered_map`.
- Abseil docs, `absl::flat_hash_map`.
- *C++ High Performance, 2nd Ed.*, Chapter 4.

*Linked: [`TODO.md`](./TODO.md) · [`NOTES.md`](./NOTES.md) · code in `./code/`*
