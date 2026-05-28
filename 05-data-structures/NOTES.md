# Chapter 4 — Data Structures
> C++ High Performance (2nd Ed.) | Björn Andrist & Viktor Sehr
> Repo folder: `cpp-high-performance/05-data-structures/`

---

## BFS CONCEPT MAP (start here — breadth first)

```
Data Structures
├── 1. The Properties Standard Containers Must Provide
│   ├── Value semantics (objects own their storage)
│   ├── Iterator support + iterator invalidation rules
│   ├── size/empty/swap noexcept
│   └── Allocator awareness
├── 2. Sequence Containers
│   ├── std::array<T,N>       — stack, fixed size, contiguous
│   ├── std::vector<T>        — heap, dynamic, contiguous, **default choice**
│   ├── std::deque<T>         — chunked map-of-blocks (random access but cache-unfriendly)
│   ├── std::list<T>          — doubly-linked nodes (rarely the right answer)
│   └── std::forward_list<T>  — singly-linked nodes
├── 3. Associative Containers (sorted, tree-based)
│   ├── std::set / std::map           — red-black tree, O(log n)
│   ├── std::multiset / std::multimap — duplicate keys allowed
│   └── Alternatives: absl::btree_map, boost::flat_map
├── 4. Unordered Associative Containers (hash tables)
│   ├── std::unordered_set / std::unordered_map
│   │   └── Standard mandates **separate chaining** → pointer chase per lookup
│   ├── Open-addressing alternatives: absl::flat_hash_map, robin_hood, boost::unordered_flat_map
│   └── Concepts: hash function, bucket, load factor, rehash
├── 5. Container Adaptors (wrap a container, restrict the API)
│   ├── std::stack<T>          — LIFO, default backend deque
│   ├── std::queue<T>          — FIFO, default backend deque
│   └── std::priority_queue<T> — heap, default backend vector
├── 6. Parallel Arrays
│   ├── AoS — Array of Structures (OO-friendly)
│   └── SoA — Structure of Arrays (vectorizable, cache-friendly when you touch one field)
└── 7. HFT-Relevant Beyond-the-Book
    ├── Cache-line alignment / false sharing (alignas, hardware_destructive_interference_size)
    ├── Lock-free SPSC ring buffers
    ├── Order-book data structures (map vs sorted-vector vs price-indexed array)
    └── Custom allocators / std::pmr / memory pools
```

---

## SECTION-BY-SECTION DEEP NOTES

### 1 — The Properties Standard Containers Must Provide

Every STL container guarantees:
1. **Value semantics** — copy and move *create independent objects*. The container *owns* the storage holding its elements.
2. **Iterator support** — `begin()`, `end()`, `cbegin()`, `cend()`. The category (input / forward / bidirectional / random-access / contiguous) tells you what's allowed.
3. **`size()`, `empty()`, `swap()`** — all O(1), all `noexcept` (size+empty since C++11, swap since C++17).
4. **Allocator awareness** — every container has an `Allocator` template parameter so you can replace `new/delete` with a pool, an arena, a `std::pmr` resource, etc.

**Iterator invalidation cheat-sheet (memorize for interviews):**

| Container         | Insert / push_back invalidates                 | Erase invalidates                              |
|-------------------|-------------------------------------------------|-------------------------------------------------|
| `vector`          | all iterators **if reallocation**; else end     | the erased element and everything after        |
| `deque`           | **all** iterators (chunks may move)             | all iterators                                  |
| `list`/`forward_list` | none                                        | only the erased element's iterator             |
| `map`/`set`       | none                                            | only the erased element's iterator             |
| `unordered_*`     | **all** iterators on rehash; else none          | only the erased element's iterator             |

> The classic HFT bug: hold a `vector::iterator` across a `push_back`. If `push_back` reallocates, your iterator dangles. Address Sanitizer catches it; the interviewer respects you for citing it.

---

### 2 — Sequence Containers

#### std::vector — your **default** container

Memory layout (3 pointers, 24 B on a 64-bit system):

```
vector<int>   ─►  ┌─────────────┐
                  │ begin       │ ─► [10][20][30][..][..][..][..][..]   heap
                  │ end         │ ─────────────────^
                  │ end_of_cap  │ ───────────────────────────────────^
                  └─────────────┘
```

* `size() = end − begin`
* `capacity() = end_of_cap − begin`
* `push_back`: if `end == end_of_cap`, allocate a new buffer of size `growth_factor × capacity`, **move** all elements (or copy if move is not noexcept), free the old buffer, then place the new element.
* Growth factor: **2× on libstdc++/libc++, 1.5× on MSVC.** Why not the golden ratio (~1.618)? You can prove that a factor < 2 lets the next allocation re-use the freed pages, but `2` keeps the math simple and gives a strict amortized O(1) guarantee. The choice is implementation-defined.
* Amortized cost: total work for n `push_back`s = `n + n/2 + n/4 + ... ≈ 2n` → **O(1) amortized** per push.
* `reserve(N)` lets you pay the allocation **once** up front — mandatory in HFT hot paths.
* `shrink_to_fit()` is a *non-binding request* (most implementations honor it but the spec doesn't force them to).

**Critical detail (HFT relevance):** `vector::push_back` reallocation copies elements. If `T`'s move constructor is `noexcept`, the standard lets vector **move** them; otherwise it must **copy** (so that the strong exception guarantee holds). This is why every type you store in a vector should have a `noexcept` move constructor.

#### std::array — stack-allocated, compile-time size

`std::array<T,N>` is essentially a `T[N]` wrapped in a class with begin/end. Zero overhead. Used in HFT for fixed-size buffers (e.g. fixed-length FIX message fields). `constexpr`-friendly.

#### std::deque — double-ended queue

Implemented as a **map of fixed-size blocks** (libstdc++ block ≈ 512 B; libc++ block ≈ 4096 B):

```
            ┌───┬───┬───┬───┬───┐         (the "map" — pointer table)
   map  ───►│ ▲ │ ▲ │ ▲ │ ▲ │ ▲ │
            └─│─┴─│─┴─│─┴─│─┴─│─┘
              ▼   ▼   ▼   ▼   ▼
            [.][.][.][.][.] (each is a chunk of T)
```

* `push_front` and `push_back` are O(1) **without** invalidating pointers to existing elements (huge win over vector for FIFO).
* But iteration jumps across chunks → many cache lines for big deques.
* `sizeof(std::deque<int>)` on libstdc++ ≈ 80 B (5 pointers + a counter).

**HFT take:** rarely the right answer. If you want O(1) push/pop both ends, you usually want a **bounded ring buffer**, which is faster and deterministic.

#### std::list / std::forward_list

Doubly- and singly-linked nodes. Each node = heap allocation = cache miss. The *only* time you reach for these in modern C++ is when you need **intrusive lists** for O(1) erase given an iterator (e.g. orders inside an order-book level — see Topic 9).

#### Why "vector almost always wins"

Even when theoretical complexity says `list::insert(it)` is O(1) and `vector::insert(it)` is O(n), the **constant factor difference** is massive: vector touches one cache line per pop, list touches one cache line **per element** (the `next` pointer dereference is a random load). On modern CPUs, traversing a vector of 10k ints is *faster* than traversing a list of 100 ints with random allocations.

---

### 3 — Associative Containers (std::map / std::set)

Internally a **red-black tree** — a self-balancing BST where each node has a color bit. Invariants ensure height ≤ 2·log₂(n+1), so all ops are O(log n).

```
                [40]B
               /     \
            [20]R   [60]R
            / \      / \
         [10][30] [50][70]
```

Each node holds: parent ptr, left ptr, right ptr, color bit, plus the key and value. On a 64-bit system that's ~48 bytes of overhead per element, scattered across the heap → poor cache locality.

**Operations & complexity:**
* `find`, `insert`, `erase`: O(log n)
* `lower_bound(k)`: smallest iterator ≥ k — O(log n)
* `upper_bound(k)`: smallest iterator > k — O(log n)
* Iteration order: **sorted ascending** by key (because in-order traversal of a BST is sorted).

**Iterator stability:** **inserts and erases of *other* keys preserve all iterators.** This is why `std::map` is popular in HFT despite being slow — you can hold pointers to nodes across modifications.

**When `map` is the wrong tool:**
* You don't need sorted order → `unordered_map` (or better, a flat hash map).
* You can keep the data in a sorted contiguous array → `boost::container::flat_map` (binary search, cache-friendly).
* You have a bounded integer key range → just use an array indexed by the key.

#### Why `absl::btree_map` is faster than `std::map`

A B-tree node holds *many* keys (e.g. 32) packed in a contiguous block. Each tree descent loads ~1 cache line and compares 32 keys → fewer cache misses per descent than a red-black tree where each descent = 1 key per cache line. Alexander Stepanov (the creator of STL) said publicly that an in-memory B*-tree is **strictly better** than a red-black tree for `std::map`.

---

### 4 — Unordered Associative Containers (hash tables)

#### How `std::unordered_map` is laid out

The C++ standard *effectively mandates* **separate chaining**:

```
buckets[]:  [▼] [_] [▼] [▼] [_] [_] [▼] [_]
             │       │   │           │
             ▼       ▼   ▼           ▼
            node    node node       node
             │       │   │           │
             ▼       ▼   ▼           ▼
            node    -    -           -
```

* `buckets` is an array of head pointers.
* Each bucket is a **singly-linked list** of nodes.
* A lookup hashes the key → indexes into `buckets` → walks the list checking each key with `==`.

This layout is forced by three standard requirements:
1. **Iterator stability** on insert (open-addressing tables can't promise this).
2. The **bucket interface** (`bucket(k)`, `begin(n)`, `end(n)`).
3. **Reference stability** to elements after insert (no rehash).

**The cost:** every lookup is a **pointer chase**. The bucket header lives in one cache line, but each node lives wherever the allocator put it. On a cache-cold lookup of a 1M-entry table, you pay one or two random L3 reads per operation.

**Load factor & rehash:**
* `load_factor() = size() / bucket_count()`
* `max_load_factor()` defaults to **1.0**.
* When `load_factor()` would exceed `max_load_factor()` on insert, the table rehashes: allocates a bigger bucket array (typically ≈ 2× the prime in libstdc++), reinserts every element, frees the old array → that single insert becomes O(n).
* `reserve(N)` does `rehash(ceil(N / max_load_factor))` once up front — call it whenever you know n.

#### Why `absl::flat_hash_map` (and friends) are faster

They use **open addressing**: one element per slot, probe linearly (or with quadratic/Robin Hood logic) on collision. Layout:

```
slots:  [k0,v0][_][k2,v2][k3,v3][_][k5,v5][_][k7,v7]
```

Every slot is in the same flat array. A lookup hashes → indexes → checks the slot → if mismatch, walks adjacent slots **in the same cache line**. Typical lookup: 1 cache miss instead of 2-3.

The trade-off: **no iterator/reference stability** — rehash moves everything, so you can't hold pointers across inserts. For HFT, this is almost always an acceptable trade.

**Robin Hood probing** is a variant that keeps the **variance** of probe distance low by swapping out "rich" entries (those near their ideal bucket) in favor of "poor" entries (those far from theirs) during insert. Cuts p99 latency dramatically.

---

### 5 — Container Adaptors

`std::stack`, `std::queue`, `std::priority_queue` are **wrappers** that restrict an underlying container's interface. The signature:

```cpp
template<class T, class Container = std::deque<T>>
class queue;
```

* `std::stack<T>` — default backend `std::deque<T>` (chunked). Faster with `std::vector<T>` as the backend (specify it).
* `std::queue<T>` — default backend `std::deque<T>` (needs `pop_front`, which `vector` doesn't have efficiently).
* `std::priority_queue<T>` — default backend `std::vector<T>`. Uses `std::make_heap`/`push_heap`/`pop_heap` internally. **You can rip out the array and treat it as a binary heap** — implicit in `[0..n)`.

**Implicit binary heap layout** (used by `priority_queue<T, vector<T>>`):

```
indices:  0   1   2   3   4   5   6
values:  [50][30][40][10][20][15][25]

parent(i) = (i-1)/2     left(i) = 2i+1     right(i) = 2i+2
```

This is one of the most cache-friendly tree structures: pure array, no pointers.

---

### 6 — Parallel Arrays (SoA vs AoS)

```cpp
// AoS — what you usually write first
struct Order { double price; int qty; uint64_t id; bool side; };
std::vector<Order> orders;        // sizeof(Order) = 24 (with padding)

// SoA — what you write when you care about cache
struct OrdersSoA {
    std::vector<double>   prices;
    std::vector<int>      qtys;
    std::vector<uint64_t> ids;
    std::vector<bool>     sides;
};
```

Why SoA wins for "give me the sum of all prices":
* AoS: each `Order` is 24 B, but you only read 8 B of it per element → you waste 67% of every cache line you load.
* SoA: `prices` is dense `double`s → every byte loaded is used → SIMD-friendly → the compiler can `vaddpd` 4 doubles per cycle on AVX.

Why AoS sometimes wins: if your hot loop touches *every* field of *every* object (e.g. "serialize this order to FIX"), AoS has all fields on the same cache line → fewer pages walked.

**Rule of thumb:** if your hot loop reads ≥ 80% of the bytes of a struct, AoS is fine; otherwise, SoA.

---

### 7 — Cache Lines, False Sharing, `alignas`

A **cache line** is the unit of transfer between RAM and L1/L2/L3 cache. On x86 it's 64 B; on Apple M-series it's 128 B. When the CPU writes a single byte, it pulls the whole containing line into L1 and marks it modified (Modified-Exclusive-Shared-Invalid → MESI protocol).

**False sharing** happens when two threads write to two **different** variables that happen to share a cache line: each write forces the other thread's cache copy to be invalidated → coherence traffic → throughput collapses.

```cpp
struct BadCounters {
    std::atomic<uint64_t> producer_count;   // both on the same
    std::atomic<uint64_t> consumer_count;   // 64-byte cache line!
};

struct GoodCounters {
    alignas(64) std::atomic<uint64_t> producer_count;
    alignas(64) std::atomic<uint64_t> consumer_count;
};
```

Use `std::hardware_destructive_interference_size` (C++17) for portability — but be aware Apple Silicon reports 128 B.

---

## CONNECTIONS TO PREVIOUS CHAPTERS
* From [[book_ch3_measuring_performance]]: amortized O(1) `push_back` is what makes vector practical — and your `ScopedTimer` is what proves it on real hardware.
* From [[book_ch2_essential_cpp_techniques]]: containers store values, so types should have `noexcept` move constructors (so vector can move on resize rather than copy).
* From [[book_ch1_brief_intro_cpp]]: value semantics + RAII is *why* STL containers are safe (destructor handles cleanup; no manual delete).

---

## KEY TAKEAWAYS

1. **Default to `std::vector`.** Contiguous, cache-friendly, amortized O(1) push, `reserve()` removes the amortization.
2. **`std::deque`, `std::list` are niche.** Reach for them only with a measured reason.
3. **`std::map` is O(log n) but slow** — red-black tree has poor locality. Consider `boost::flat_map` or `absl::btree_map`.
4. **`std::unordered_map` is slower than `absl::flat_hash_map`** because the standard mandates separate chaining. In HFT, prefer an open-addressing map.
5. **Iterator invalidation rules vary per container.** Know them cold.
6. **`alignas(64)` everything that's shared between threads** to avoid false sharing.
7. **Choose layout (AoS vs SoA) by the hot-loop access pattern**, not by what reads nicely.
8. **For HFT order books: array indexed by price tick when possible**, sorted vector + binary search otherwise, `std::map` only for prototypes.

---

*Linked: [`TODO.md`](./TODO.md) · [`05_data_structures_deep_dive.md`](./05_data_structures_deep_dive.md)*
