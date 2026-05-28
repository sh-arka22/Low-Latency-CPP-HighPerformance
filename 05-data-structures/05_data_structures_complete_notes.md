# Chapter 4 — Data Structures: Complete Deep Dive

> C++ High Performance (2nd Ed.) | Björn Andrist & Viktor Sehr
> Folder: `cpp-high-performance/05-data-structures/`
> Code: [code/](file:///Users/arkaj/Desktop/Low-Latency-CPP/mini_quote_engine/cpp-high-performance/05-data-structures/code)

---

> [!IMPORTANT]
> This document explains **every section** of the [NOTES.md](file:///Users/arkaj/Desktop/Low-Latency-CPP/mini_quote_engine/cpp-high-performance/05-data-structures/NOTES.md) in depth — the **what**, the **why**, the **how it works at the memory-layout level**, and production-grade C++ code that demonstrates each concept. Treat this as the single source of truth for Chapter 4.

---

## Table of Contents

1. [The Properties Standard Containers Must Provide](#1--the-properties-standard-containers-must-provide)
2. [Sequence Containers](#2--sequence-containers)
3. [Associative Containers (std::map / std::set)](#3--associative-containers-stdmap--stdset)
4. [Unordered Associative Containers (Hash Tables)](#4--unordered-associative-containers-hash-tables)
5. [Container Adaptors](#5--container-adaptors-stack-queue-priority_queue)
6. [Parallel Arrays — AoS vs SoA](#6--parallel-arrays--aos-vs-soa)
7. [Cache Lines, False Sharing, alignas](#7--cache-lines-false-sharing-alignas)
8. [Lock-Free SPSC Ring Buffer](#8--lock-free-spsc-ring-buffer)
9. [Order-Book Data Structures](#9--order-book-data-structures)
10. [Allocators & Memory Pools](#10--allocators--memory-pools)

---

## 1 — The Properties Standard Containers Must Provide

### What the notes say

Every STL container guarantees four things: **(a) value semantics**, **(b) iterator support**, **(c) `size()`/`empty()`/`swap()` are O(1) and noexcept**, **(d) allocator awareness**.

### Why these four matter — in depth

#### (a) Value Semantics — containers OWN their elements

**What it means:**
When you copy a `std::vector<int> v2 = v1;`, you get a **completely independent** object. Modifying `v2` never touches `v1`. The container "owns" the storage — it allocates, constructs, destructs, and deallocates everything internally.

**Why it matters:**
Value semantics is the foundation of C++'s safety model. Combined with RAII (Chapter 1), it means:
- No manual `delete` — the destructor handles cleanup.
- No accidental aliasing — copies are deep by default.
- Move semantics (Chapter 2) let you transfer ownership cheaply when you don't need a copy.

**The trade-off:**
Value semantics means copying a `vector<Order>` of 1M orders copies all 1M orders. This is why moves exist — and why you mark move constructors `noexcept` (more on this in §2).

```cpp
// Value semantics in action
std::vector<int> v1 = {1, 2, 3};
std::vector<int> v2 = v1;          // DEEP COPY — allocates new heap memory
v2[0] = 99;
assert(v1[0] == 1);                // v1 is untouched
// When v1 and v2 go out of scope, each frees its own memory — no double-free.
```

#### (b) Iterator Support — the uniform traversal abstraction

**What it means:**
Every container exposes `begin()`, `end()`, `cbegin()`, `cend()`, `rbegin()`, `rend()`. These return iterators — lightweight objects that point into the container and support at minimum `*it` (dereference), `++it` (advance), and `it1 == it2` (equality).

**The iterator hierarchy — and why it matters:**

```
contiguous_iterator  ⊂  random_access_iterator  ⊂  bidirectional_iterator  ⊂  forward_iterator  ⊂  input_iterator
       ↑                      ↑                        ↑                        ↑
    vector, array,         deque                   list, map, set           forward_list,
    string, span                                                           unordered_*
```

| Category | Supported operations | Example container |
|----------|---------------------|-------------------|
| **Contiguous** | `it + n`, `it - n`, `it[n]`, **and** elements are in a flat array | `vector`, `array`, `string` |
| **Random access** | `it + n`, `it - n`, `it[n]` | `deque` |
| **Bidirectional** | `++it`, `--it` | `list`, `map`, `set` |
| **Forward** | `++it` only | `forward_list`, `unordered_*` |

**Why the distinction:**
Algorithms like `std::sort` require `random_access_iterator` (it needs `it[n]` to do partitioning). `std::lower_bound` on a `random_access_iterator` does O(log n) comparisons AND O(log n) advances; on a `forward_iterator` it does O(log n) comparisons but O(n) advances.

```cpp
#include <concepts>

// C++20: you can constrain on iterator category
template<std::random_access_iterator Iter>
void my_sort(Iter first, Iter last) {
    // std::sort will work — we have O(1) random access
    std::sort(first, last);
}

std::vector<int> v = {3, 1, 2};
my_sort(v.begin(), v.end());      // ✓ compiles — contiguous ⊂ random_access

std::list<int> l = {3, 1, 2};
// my_sort(l.begin(), l.end());   // ✗ compile error — bidirectional ≠ random_access
```

#### (c) `size()`, `empty()`, `swap()` — O(1) and `noexcept`

**What it means:**
- `size()` returns the number of elements in O(1). (Before C++11, `std::list::size()` was allowed to be O(n); since C++11 it must be O(1) — that's why `splice()` became O(n) for sub-ranges.)
- `empty()` returns `size() == 0` in O(1).
- `swap()` exchanges two containers in O(1) — just swapping internal pointers, not copying elements. Marked `noexcept` since C++17.

**Why `swap()` being noexcept matters in HFT:**
A swap is 3 pointer exchanges (for vector: `begin_`, `end_`, `end_of_cap_`). No allocations, no copies, no exceptions. This is essential for lock-free double-buffering patterns: one thread writes to buffer A while the other reads buffer B, then they swap atomically.

```cpp
// O(1) swap — no copies, no throws
std::vector<int> a = {1, 2, 3, 4, 5};  // 1M elements in reality
std::vector<int> b = {6, 7, 8, 9, 10};
a.swap(b);   // swaps 3 pointers ≈ 24 bytes — O(1), noexcept, regardless of N
```

#### (d) Allocator Awareness — every container lets you replace `new/delete`

**What it means:**
Every standard container has an `Allocator` template parameter:

```cpp
template<class T, class Allocator = std::allocator<T>>
class vector;
```

By default it uses `std::allocator<T>`, which calls `::operator new` / `::operator delete`. But you can replace it with a pool allocator, a stack arena, or `std::pmr::polymorphic_allocator`.

**Why this matters for HFT:**
`::operator new` is non-deterministic. It can:
- Take a lock (in the global allocator — glibc's `ptmalloc2` or jemalloc).
- Trigger a `mmap` system call (kernel mode switch ≈ 1-10 μs).
- Cause a page fault (TLB miss → page walk ≈ 1-10 μs).

Custom allocators eliminate all of this. See [§10](#10--allocators--memory-pools) for implementation.

---

### Iterator Invalidation — the full truth

> [!CAUTION]
> Iterator invalidation is the #1 source of bugs in production C++ code and the #1 interview topic for containers. The table below gives you the WHAT; the paragraphs after give you the **WHY**.

| Container | Insert / push_back invalidates | Erase invalidates |
|---|---|---|
| `vector` | **all** if reallocation; else only `end()` | the erased element + everything after |
| `deque` | **all iterators** (but NOT references/pointers to elements, if inserting at front/back) | **all iterators** |
| `list` / `forward_list` | **none** | only the erased element's iterator |
| `map` / `set` | **none** | only the erased element's iterator |
| `unordered_*` | **all** on rehash; else none | only the erased element's iterator |

**Why vector invalidates everything on realloc:**

When `push_back` exhausts capacity, vector allocates a new buffer (at a different address), moves all elements into it, and frees the old buffer. Every iterator was a pointer into the old buffer → every iterator is now dangling.

```cpp
// The classic bug — holding an iterator across push_back
std::vector<int> v = {1, 2, 3};
v.reserve(3);                      // capacity == 3
int* ptr = &v[0];                  // points into the old buffer
v.push_back(4);                    // capacity exhausted → realloc → old buffer freed
// *ptr is now a USE-AFTER-FREE.  ASan catches this; production code silently corrupts.
```

**Why deque invalidates all iterators but NOT references:**

Deque's iterators contain a pointer into the "map" (the array of chunk pointers) plus an offset. When you push to the front/back, the map may be reallocated → the iterator's map pointer is stale. But the *elements themselves* don't move (they stay in their chunks) → pointers/references to elements remain valid.

**Why list/map never invalidate on insert:**

Each element lives in its own heap-allocated node. Inserting a new node creates a new allocation and adjusts prev/next pointers — existing nodes don't move.

**Why unordered containers invalidate on rehash:**

Rehash reallocates the bucket array and re-links every node into new buckets. The node pointers stay valid (the node objects don't move), but the bucket structure changes → iterators that encode a bucket index become invalid.

> See [01_sequence_containers.cpp](file:///Users/arkaj/Desktop/Low-Latency-CPP/mini_quote_engine/cpp-high-performance/05-data-structures/code/01_sequence_containers.cpp) `section_iterator_invalidation()` for a live demonstration.

---

## 2 — Sequence Containers

### 2.1 — `std::vector<T>` — Your Default Container

#### The 3-Pointer Layout

`std::vector<T>` is 24 bytes on a 64-bit system (three raw pointers):

```
        stack / class memory (24 B)          heap allocation
        ┌──────────────┐
vector  │  begin_      ─┼─►  ┌────┬────┬────┬────┬────┬────┬────┬────┐
        │  end_        ─┼────────────────────^                        │
        │  end_of_cap_ ─┼────────────────────────────────────────────^│
        └──────────────┘      T[0] T[1] T[2] T[3]  ...  T[size-1]    capacity
                                   contiguous heap buffer
```

```cpp
// Verify on your system:
#include <iostream>
#include <vector>

int main() {
    std::cout << "sizeof(vector<int>) = " << sizeof(std::vector<int>) << "\n";
    // → 24 on gcc/clang x86-64

    std::vector<int> v = {10, 20, 30};
    std::cout << "size     = " << v.size()     << "\n";  // 3 — end_ - begin_
    std::cout << "capacity = " << v.capacity() << "\n";  // ≥ 3 — end_of_cap_ - begin_
}
```

> **Why 3 pointers and not `{pointer, size, capacity}`?**
> Three pointers let `push_back` compare `end_ == end_of_cap_` in one instruction (pointer compare) instead of computing `begin_ + size_ vs begin_ + cap_`. Micro-optimization that matters in tight loops.

#### Growth Factor — Interview Gold

When `push_back` exhausts capacity, vector allocates a new buffer of size `growth_factor × old_capacity`:

| STL implementation | Growth factor | Rationale |
|---|---|---|
| libstdc++ (gcc) | **2.0** | Simple amortized O(1) proof; easy to reason about |
| libc++ (clang) | **2.0** | Same |
| MSVC (Microsoft) | **1.5** | Freed pages can be reused by the next allocation (when `r < 2`, the sum of previously freed buffers eventually exceeds the next allocation) |

**The mathematical proof of amortized O(1):**

Starting from capacity 1, n `push_back` calls trigger reallocations at capacities 1, 2, 4, 8, ..., up to n. The total elements copied across all reallocations:

```
1 + 2 + 4 + ... + n = 2n − 1 = O(n)
```

So n pushes = O(n) total work → **O(1) amortized per push**.

**Why any factor r > 1 works:** The total copy work is a geometric series:
```
n + n/r + n/r² + ... = n · r/(r-1)
```
which is O(n) for any constant `r > 1`.

**Why the golden ratio (~1.618) is theoretically nice:**
With factor `r`, after freeing blocks of sizes 1, r, r², ..., rᵏ, the sum of all freed blocks is `(rᵏ⁺¹ − 1) / (r − 1)`. For the next allocation of size `rᵏ⁺¹` to fit into the freed space, we need this sum ≥ `rᵏ⁺¹`. Solving: `r ≤ (1 + √5) / 2 ≈ 1.618` (the golden ratio). MSVC's 1.5 satisfies this; gcc's 2.0 does not (the freed blocks are never quite big enough to reuse).

In practice, memory fragmentation is complex and allocator-specific — so both 1.5 and 2.0 work fine.

```cpp
// Observe the growth factor empirically
static void observe_growth_factor() {
    std::vector<int> v;
    size_t last_cap = v.capacity();
    for (int i = 0; i < 1'000'000; ++i) {
        v.push_back(i);
        if (v.capacity() != last_cap) {
            double ratio = double(v.capacity()) / std::max(last_cap, size_t(1));
            std::cout << "cap " << last_cap << " → " << v.capacity()
                      << "  (ratio = " << ratio << ")\n";
            last_cap = v.capacity();
        }
    }
    // On gcc: you'll see ratios of exactly 2.0
    // On MSVC: you'll see ratios of exactly 1.5
}
```

> See [01_sequence_containers.cpp](file:///Users/arkaj/Desktop/Low-Latency-CPP/mini_quote_engine/cpp-high-performance/05-data-structures/code/01_sequence_containers.cpp) `section_vector_growth()`.

#### `push_back` — Step by Step, with the `noexcept` Move Trick

This is how `push_back` actually works inside the standard library:

```cpp
template<class T>
void vector<T>::push_back(T&& val) {
    if (end_ == end_of_cap_) {                  // Step 1: check capacity
        size_t old_cap = capacity();
        size_t new_cap = old_cap ? 2 * old_cap : 1;
        T* new_buf = alloc_.allocate(new_cap);   // Step 2: allocate new buffer

        // Step 3: transfer existing elements
        if constexpr (std::is_nothrow_move_constructible_v<T>)
            std::uninitialized_move(begin_, end_, new_buf);   // MOVE — fast
        else
            std::uninitialized_copy(begin_, end_, new_buf);   // COPY — slow, safe

        // Step 4: destroy old elements, free old buffer
        std::destroy(begin_, end_);
        alloc_.deallocate(begin_, old_cap);

        // Step 5: update pointers
        begin_       = new_buf;
        end_         = new_buf + old_cap;
        end_of_cap_  = new_buf + new_cap;
    }
    ::new (static_cast<void*>(end_)) T(std::move(val));   // Step 6: placement new
    ++end_;
}
```

**Why `noexcept` move matters — the strong exception guarantee:**

If `T`'s move constructor can throw, vector can't use it during reallocation. Here's why:

1. Vector starts moving elements from the old buffer to the new buffer.
2. Halfway through (say, element 500 of 1000), the move constructor throws.
3. The first 500 elements have been *moved* — their old copies are in a "moved-from" state (probably destroyed).
4. To roll back, you'd need to move them *back* — but that might throw too.
5. The strong exception guarantee says: if `push_back` throws, the container is unchanged. You can't guarantee this if moves threw halfway.

**Solution:** if `T`'s move is `noexcept`, vector moves. If it might throw, vector copies (copies don't destroy the source, so rollback is trivial: just destroy the partial new copies).

```cpp
// BAD — no noexcept on move ctor → vector COPIES on resize
struct BadOrder {
    std::string data;
    BadOrder(BadOrder&& o) : data(std::move(o.data)) {}   // might throw (no noexcept)
};

// GOOD — noexcept on move ctor → vector MOVES on resize
struct GoodOrder {
    std::string data;
    GoodOrder(GoodOrder&& o) noexcept : data(std::move(o.data)) {}
};

// In HFT: "death by 1000 copies" — every resize copies 1M orders instead of moving them.
// Rule: ALWAYS mark move constructors noexcept.
```

#### `reserve()` and `shrink_to_fit()`

```cpp
std::vector<int> v;
v.reserve(1'000'000);   // allocate once, up front — capacity is now 1M
// Next 1M push_backs: zero reallocations, zero copies.
// This is MANDATORY in HFT hot paths.

v.shrink_to_fit();       // REQUEST to release unused capacity
// Non-binding: the standard says the implementation MAY ignore it.
// Most implementations honor it, but you can't rely on it in the standard.
```

**Why `reserve` is mandatory in HFT:**
Without `reserve`, a vector of 1M elements does ~20 reallocations (log₂(1M) ≈ 20). Each reallocation: (1) `malloc` a new buffer → system call → non-deterministic latency; (2) copy/move all elements → O(n) work; (3) `free` the old buffer → another system call. With `reserve`: zero reallocations, zero system calls on the hot path.

---

### 2.2 — `std::array<T, N>` — Stack-Allocated, Compile-Time Size

```cpp
std::array<int, 4> a = {1, 2, 3, 4};
static_assert(sizeof(a) == 16);        // exactly sizeof(int) * 4 — ZERO overhead
// No heap allocation. No pointers. No size counter. Just T[N] wrapped in a struct.
```

**Why it exists (instead of raw `T[N]`):**
- Has `begin()`, `end()`, `size()` — works with all STL algorithms.
- Has `.at(i)` for bounds-checked access.
- Is `constexpr`-friendly — can be filled at compile time.
- Supports structured bindings: `auto [a, b, c, d] = arr;`
- Doesn't decay to a pointer (unlike C arrays passed to functions).

**HFT uses:**
- Fixed-length FIX protocol message fields.
- Fixed-size ring buffer slots.
- Lookup tables (e.g., `constexpr std::array<double, 256> sin_table = ...;` baked into the binary).

```cpp
// constexpr lookup table — computed at compile time, zero runtime cost
constexpr std::array<int, 5> primes = {2, 3, 5, 7, 11};
static_assert(primes[2] == 5);     // verified at compile time
```

---

### 2.3 — `std::deque<T>` — Chunked Map-of-Blocks

#### Memory Layout

```
              ┌────────────────────────────────┐
       map ──►│  blk*   blk*   blk*   blk*    │   (pointer table — "the map")
              └──┬──────┬──────┬──────┬────────┘
                 ▼      ▼      ▼      ▼
              [T T T] [T T T] [T T T] [T T T]    (each chunk is T[chunk_size])
```

- **libstdc++ (gcc):** chunk size = `max(512 / sizeof(T), 1)` elements per chunk.
- **libc++ (clang):** chunk size = `max(4096 / sizeof(T), 16)` elements per chunk.
- **MSVC:** chunk size = `max(16 / sizeof(T), 1)` — very small chunks → very poor cache performance.

```cpp
std::cout << "sizeof(deque<int>) = " << sizeof(std::deque<int>) << "\n";
// → 80 on libstdc++ (gcc) — map pointer, 4 iterators, size counter
// Compare: sizeof(vector<int>) = 24
```

**Why deque exists — O(1) push_front:**
- `push_front`: add an element to the beginning of the first chunk (or allocate a new chunk at the front). O(1) amortized.
- `push_back`: add an element to the end of the last chunk (or allocate a new chunk at the back). O(1) amortized.
- **Key property:** elements DON'T MOVE when you push at either end. References/pointers to elements stay valid (though iterators may invalidate because they index through the map).

**Why deque is SLOW for iteration:**
A deque iterator must check at every `++it`: "am I at the end of this chunk? If so, jump to the next chunk." That's a branch per advance. In contrast, a vector iterator is just a pointer bump — branch-free, prefetcher-friendly.

**Why deque is almost always wrong in HFT:**
- Use `std::vector` for stack-like patterns (push/pop at back only).
- Use a **ring buffer** for queue-like patterns (push at back, pop from front) — fixed capacity, no allocations, no branch per advance.
- Use `std::deque` only if you truly need O(1) push_front with reference stability, which is rare.

---

### 2.4 — `std::list<T>` / `std::forward_list<T>` — Linked Nodes

```
list<T>  head ── [prev|T|next] ⇄ [prev|T|next] ⇄ [prev|T|next] ── tail
                    16 B overhead per node (2 pointers) + sizeof(T) + padding
```

Each node is a **separate heap allocation**. On a 64-bit system:
- `std::list<int>` node = 2 pointers (16 B) + `int` (4 B) + padding = **32 B per node** (for 4 B of actual data).
- That 32 B is at a random heap address → a cache miss per node on traversal.

```cpp
// The performance disaster
std::list<int> l(100);      // 100 nodes, each heap-allocated at random addresses
int sum = 0;
for (auto x : l) sum += x;  // 100 pointer chases → 100 potential cache misses
// vs vector:
std::vector<int> v(100);    // 100 ints in a flat array → ~2 cache lines
for (auto x : v) sum += x;  // sequential reads → hardware prefetcher fetches ahead
```

**When list IS the right answer:**
1. **O(1) splice:** You can move a range of elements from one list to another in O(1) — just re-link 4 pointers. No copies, no moves. This is impossible with contiguous containers.
2. **O(1) erase given an iterator:** If you already have an iterator to the element, erasing it is O(1). (With vector, erasing the middle is O(n) because you shift everything after.)
3. **Intrusive lists** — the HFT pattern. Instead of `std::list` (which heap-allocates nodes), you embed prev/next pointers inside your `T` itself:

```cpp
// Intrusive list — orders inside an order-book level
struct Order {
    Order* prev_ = nullptr;    // intrusive: belongs to the Level's list
    Order* next_ = nullptr;
    uint64_t id;
    int64_t  qty;
    int      price;
};
// No separate node allocation — the Order IS the node.
// cancel(order_ptr) = unlink(order_ptr) = O(1)
```

**Vector vs List — the empirical truth:**

> See [01_sequence_containers.cpp](file:///Users/arkaj/Desktop/Low-Latency-CPP/mini_quote_engine/cpp-high-performance/05-data-structures/code/01_sequence_containers.cpp) `section_traversal_bench()`.

Traversing a `vector<int>` of 10M elements is **5-20× faster** than traversing a `list<int>` of 10M elements, even though both are "O(n)". The constant factor is everything:

| | `vector<int>` | `list<int>` |
|---|---|---|
| Bytes per element | 4 (just the int) | ~32 (int + 2 pointers + padding) |
| Cache lines per 10M elements | ~625K (contiguous) | ~5M (random) |
| Prefetcher effectiveness | Perfect (sequential access) | Zero (random access) |
| Branch overhead per advance | None (pointer bump) | 1 branch (dereference `next`) |

---

### 2.5 — `FixedVector<T, N>` — The HFT Static Vector

When you know the maximum size at compile time and you CANNOT tolerate heap allocations:

```cpp
template<class T, std::size_t N>
class FixedVector {
    alignas(T) std::byte storage_[sizeof(T) * N];   // stack buffer
    std::size_t size_ = 0;

public:
    T*       data()       noexcept { return reinterpret_cast<T*>(storage_); }
    const T* data() const noexcept { return reinterpret_cast<const T*>(storage_); }
    std::size_t size()     const noexcept { return size_; }
    static constexpr std::size_t capacity() noexcept { return N; }

    bool push_back(const T& v) {
        if (size_ == N) return false;            // bounded — never throws
        ::new (data() + size_) T(v);             // placement new
        ++size_;
        return true;
    }
    void pop_back() {
        if (size_ == 0) return;
        --size_;
        data()[size_].~T();                      // explicit destructor call
    }
    T& operator[](std::size_t i) { return data()[i]; }
    ~FixedVector() {
        for (std::size_t i = 0; i < size_; ++i)
            data()[i].~T();                      // destroy all live elements
    }
};
```

**Why this matters in HFT:**
- Zero heap allocations → zero `malloc` latency.
- Fixed stack footprint → deterministic.
- Used for: FIX message field buffers, small order batches, side channels.

> See [01_sequence_containers.cpp](file:///Users/arkaj/Desktop/Low-Latency-CPP/mini_quote_engine/cpp-high-performance/05-data-structures/code/01_sequence_containers.cpp) `section_fixed_vector()`.

---

## 3 — Associative Containers (std::map / std::set)

### The Data Structure: Red-Black Tree

`std::map<K,V>` is backed by a **red-black tree** — a self-balancing BST. Every node stores:

```cpp
// What a red-black tree node looks like (simplified):
struct rb_node {
    rb_node* parent;           // 8 B
    rb_node* left;             // 8 B
    rb_node* right;            // 8 B
    enum { red, black } color; // 1 B (padded to 8 B typically)
    std::pair<const K, V> kv;  // sizeof(K) + sizeof(V)
};
// Overhead per node: 3 pointers + color ≈ 32 B, each node is a separate heap allocation
```

### The 5 Red-Black Tree Invariants

1. Every node is either **red** or **black**.
2. The **root** is always **black**.
3. Every leaf (NIL sentinel) is **black**.
4. If a node is **red**, both its children are **black** (no two consecutive reds on any path).
5. Every path from the root to a leaf has the **same number of black nodes** (the "black height").

**Why these guarantee O(log n):**
Invariants 4 and 5 together ensure that the longest root-to-leaf path is at most **twice** the shortest path. The shortest path has only black nodes (all equal to the black height `b`). The longest path alternates red-black-red-black, so it has at most `2b` nodes. Therefore `height ≤ 2·log₂(n+1)`, and all operations (find, insert, erase) follow a single root-to-leaf path → O(log n).

```
             [40]B                    B = black, R = red
            /     \
         [20]R   [60]R
         / \      / \
      [10]B[30]B[50]B[70]B
```

**Why NOT AVL trees?**
AVL trees maintain stricter balance (heights of children differ by at most 1), so lookups are slightly faster (height ≤ 1.44·log₂(n+2)). But maintaining this stricter balance requires **more rotations per insert/erase**. Red-black trees do at most 2 rotations per insert and at most 3 per erase. AVL trees may do O(log n) rotations per erase. For use cases with frequent inserts/erases (like an order book), red-black trees win on total work.

### Operations and Complexity

```cpp
std::map<int, std::string> m;

// Insert: O(log n) — walk the tree to find the leaf position, insert, rebalance
m[30] = "thirty";
m.insert({20, "twenty"});
m.try_emplace(10, "ten");      // won't overwrite if key exists

// Lookup: O(log n) — binary search down the tree
auto it = m.find(20);          // returns iterator or end()

// Erase: O(log n) — find the node, unlink, rebalance
m.erase(20);
m.erase(it);                   // erase by iterator — O(1) amortized

// lower_bound / upper_bound: O(log n)
auto lb = m.lower_bound(25);   // first key ≥ 25 → points to 30
auto ub = m.upper_bound(25);   // first key > 25 → points to 30

// Iteration: sorted ascending by key (in-order traversal of BST)
for (auto& [k, v] : m)
    std::cout << k << " → " << v << "\n";   // prints 10, 20, 30
```

> See [02_associative_containers.cpp](file:///Users/arkaj/Desktop/Low-Latency-CPP/mini_quote_engine/cpp-high-performance/05-data-structures/code/02_associative_containers.cpp) `section_sorted_iteration()` and `section_price_ladder()`.

### The `operator[]` Trap

```cpp
std::map<std::string, int> counts;

// DANGEROUS: operator[] DEFAULT-CONSTRUCTS missing keys!
if (counts["never_seen"] == 0) {
    // This DID insert {"never_seen", 0} into the map!
    // counts.size() is now 1, not 0.
}

// SAFE: use .find() — it never inserts
auto it = counts.find("never_seen");
if (it == counts.end()) {
    // Key doesn't exist, and the map is unchanged.
}
```

**Why `operator[]` works this way:**
`operator[]` must return a `V&` (a reference to the value). If the key doesn't exist, it has no value to return a reference to — so it default-constructs one and inserts it. This is by design, but it's a trap when you're using it just to check existence.

> See [02_associative_containers.cpp](file:///Users/arkaj/Desktop/Low-Latency-CPP/mini_quote_engine/cpp-high-performance/05-data-structures/code/02_associative_containers.cpp) `section_op_bracket_trap()`.

### Why `std::map` is Slow in Practice

**Cache analysis:**
A lookup in a `std::map` of 1M elements: `log₂(1M) ≈ 20` tree levels → 20 node accesses. Each node is a separate heap allocation at a random address → each access is likely an L3 cache miss (~50-100 ns on modern hardware). Total: **20 × ~70 ns ≈ 1.4 μs per lookup**.

Compare to a **sorted vector + binary search** of 1M elements: binary search touches `log₂(1M) ≈ 20` elements, but they're in a contiguous array. The first few probes (near the middle) cause cache misses, but the later probes (in smaller and smaller ranges) hit cache. Typical: **~6 cache misses per lookup ≈ 0.4 μs**.

### Alternatives That Win

| Container | Layout | Lookup | Insert | Cache behavior |
|---|---|---|---|---|
| `std::map` | Red-black tree (heap nodes) | O(log n) | O(log n) | ~20 cache misses for 1M |
| `boost::flat_map` | Sorted vector + binary search | O(log n) | O(n) (shift) | ~6 cache misses for 1M |
| `absl::btree_map` | B-tree (multi-key nodes) | O(log n) | O(log n) | ~3-4 cache misses for 1M |

**Why B-trees are better:**
A B-tree node packs many keys (e.g., 32) into one contiguous block. Each tree descent loads one cache line and compares 32 keys via linear scan (or SIMD). So `log_32(1M) ≈ 4` cache misses per lookup vs 20 for a red-black tree.

> See [02_associative_containers.cpp](file:///Users/arkaj/Desktop/Low-Latency-CPP/mini_quote_engine/cpp-high-performance/05-data-structures/code/02_associative_containers.cpp) `section_map_vs_sorted_vector()`.

### Iterator Stability — The One Reason to Use `std::map`

Inserting or erasing any key does **NOT** invalidate iterators or pointers to other keys. Each node is an independent heap allocation — modifying the tree structure (rotations, re-linking) doesn't move any node's memory.

```cpp
std::map<int, int> m = {{1, 10}, {2, 20}, {3, 30}};
auto it = m.find(2);              // iterator to key=2
m[100] = 1000;                    // insert another key
m.erase(3);                       // erase a different key
std::cout << it->second << "\n";  // still 20 — iterator is VALID

// This is WHY HFT order books use std::map for prototypes:
// you can hold pointers to specific price levels across mutations.
```

> See [02_associative_containers.cpp](file:///Users/arkaj/Desktop/Low-Latency-CPP/mini_quote_engine/cpp-high-performance/05-data-structures/code/02_associative_containers.cpp) `section_iterator_stability()`.

---

## 4 — Unordered Associative Containers (Hash Tables)

### How `std::unordered_map` is Laid Out

The C++ standard **effectively mandates separate chaining**:

```
buckets[] (a vector<node*>):

  [▼] [_] [▼] [▼] [_] [_] [▼] [_]
   │       │   │           │
   ▼       ▼   ▼           ▼
  node    node node       node        ← each node is a SEPARATE heap allocation
   │       │   │           │
   ▼       ▼   ▼           ▼
  node     ∅   ∅          node        ← collision chain (singly-linked list)
```

A lookup does:
1. Compute `hash(key)`.
2. `bucket_index = hash(key) % bucket_count()`.
3. Walk the linked list at `buckets[bucket_index]`, comparing each node's key with `==`.
4. Return the matching node (or `end()`).

**Each step 3 node access = a potential cache miss** (node is heap-allocated at a random address).

### Why the Standard Forces This Layout

Three requirements in the standard rule out open addressing:

1. **Iterator stability on insert:** Inserting a new key must NOT invalidate iterators to existing keys. Open-addressing tables move elements during probe-sequence adjustments → can't guarantee this.
2. **Reference/pointer stability:** References to elements must stay valid as long as the element is not erased (even across inserts that cause rehash, references stay valid in the standard's wording for non-rehash inserts). With separate chaining, nodes don't move.
3. **The bucket API:** `bucket(k)`, `bucket_size(n)`, `begin(n)`, `end(n)` — these expose the bucket structure. Open-addressing tables don't have buckets.

**The consequence:** `std::unordered_map` will always be slower than open-addressing alternatives for lookup-heavy workloads.

### Load Factor and Rehash

```cpp
std::unordered_map<int, int> m;

// Key metrics:
m.bucket_count();        // number of buckets
m.size();                // number of elements
m.load_factor();         // = size() / bucket_count()
m.max_load_factor();     // default 1.0 — rehash triggers when load_factor() exceeds this

// Pre-allocate to avoid rehashes on the hot path:
m.reserve(1'000'000);   // = rehash(ceil(1M / max_load_factor()))
// Now 1M inserts without a single rehash.
```

**What happens during a rehash:**
1. Allocate a new bucket array (typically next prime ≥ 2× current bucket count in libstdc++).
2. Walk every node in every bucket. For each node: recompute `hash(key) % new_bucket_count`, unlink from old bucket, link into new bucket.
3. Free the old bucket array.
4. That single insert is O(n). Across n inserts, it amortizes to O(1).

```cpp
// Observing rehashes:
std::unordered_map<int, int> m;
size_t prev_bc = m.bucket_count();
int rehash_count = 0;
for (int i = 0; i < 1'000'000; ++i) {
    m[i] = i;
    if (m.bucket_count() != prev_bc) {
        ++rehash_count;
        prev_bc = m.bucket_count();
    }
}
std::cout << "Rehashes without reserve: " << rehash_count << "\n";   // ~20

// With reserve(1M): zero rehashes.
```

> See [03_unordered_containers.cpp](file:///Users/arkaj/Desktop/Low-Latency-CPP/mini_quote_engine/cpp-high-performance/05-data-structures/code/03_unordered_containers.cpp) `section_internals()` and `section_reserve()`.

### Cost of a Cache-Cold Lookup

```
Step 1. Hash compute:               ~5-20 ns (good hash; bad hash → 100+ ns)
Step 2. Load bucket head pointer:   ~50-100 ns (L3 miss — bucket array is large)
Step 3. Load first node:            ~50-100 ns (L3 miss — node is heap-allocated)
Step 4. Key compare:                ~1-5 ns
Step 5. If collision, load next:    +50-100 ns per chain link
────────────────────────────────────────────────────────
Total: ~100-300 ns per lookup (best case, no collision)
```

### Custom Hash Function

The default `std::hash<T>` only works for basic types. For your own structs, you must provide:

```cpp
struct OrderId {
    uint64_t client_id;
    uint32_t local_id;
    bool operator==(const OrderId&) const noexcept = default;  // C++20
};

struct OrderIdHash {
    size_t operator()(const OrderId& o) const noexcept {
        // splitmix64-style finalizer — high-quality, fast
        uint64_t x = o.client_id ^ (uint64_t(o.local_id) << 32);
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
        x =  x ^ (x >> 31);
        return size_t(x);
    }
};

std::unordered_map<OrderId, Order, OrderIdHash> book;
```

**Why splitmix64?**
- Fast: three multiplies, three XOR-shifts.
- High avalanche: changing one bit of input changes ~50% of output bits.
- Production-proven: used in Java's `SplittableRandom`, Abseil's hash mixer.

**Interview trap:** If your hash function returns a constant (e.g., `return 0;`), every key lands in the same bucket → the linked list becomes a linked list of n elements → lookups degrade from O(1) to O(n).

> See [03_unordered_containers.cpp](file:///Users/arkaj/Desktop/Low-Latency-CPP/mini_quote_engine/cpp-high-performance/05-data-structures/code/03_unordered_containers.cpp) `section_custom_hash()`.

### Open-Addressing Alternatives — Why They Win

#### `absl::flat_hash_map` layout

```
metadata[]:  [_][H7][H7][_][_][H7][_][H7][_][_]  (1 byte per slot — top 7 bits of hash)
slots[]:     [_][k,v][k,v][_][_][k,v][_][k,v]     (flat array of key-value pairs)
```

- **One element per slot**, no linked list.
- On collision, **probe** adjacent slots (linear or quadratic).
- The metadata byte stores 7 bits of the hash + 1 control bit (empty/full/deleted).
- SSE2 `PCMPEQB` compares **16 metadata bytes at once** → finds the matching slot in one SIMD instruction.

**Why this is 2-5× faster:**
- **One cache line per lookup** (metadata + slot are adjacent in memory).
- **No pointer chasing** (everything is in the flat array).
- **No per-node allocation** (50-80% lower memory usage).
- **SIMD probe** (16 candidates per cycle).

**The trade-off:**
- **No iterator stability** — rehash moves all elements. Don't hold iterators across inserts.
- **No reference stability** — `&map[key]` becomes invalid after any insert that triggers rehash.

For HFT, this trade-off is almost always acceptable.

#### Robin Hood Probing — Variance Reduction

Robin Hood probing is a refinement of open addressing where you measure the **probe distance** (how far an element is from its ideal slot). During insert, if the new element's probe distance exceeds the existing element's probe distance, you **swap them** — you steal from the "rich" (short probe distance) and give to the "poor" (long probe distance).

**Why this helps HFT:**
- **Reduces p99 latency dramatically.** Without Robin Hood, a few unlucky keys might probe 20+ slots (tail latency spikes). With Robin Hood, the maximum probe distance is kept low (the variance is bounded).
- **p99 matters more than mean in HFT.** Your matching engine runs at the speed of the worst-case lookup, not the average.

#### Hand-Rolled Open-Addressing Map

```cpp
template<class K, class V, size_t N>
class LinearMap {
    static_assert((N & (N-1)) == 0, "N must be power of two");
    struct Slot { bool occupied; K key; V value; };
    std::array<Slot, N> slots_{};

public:
    LinearMap() { for (auto& s : slots_) s.occupied = false; }

    void insert(const K& k, const V& v) {
        size_t i = std::hash<K>{}(k) & (N - 1);         // bitmask modulo
        while (slots_[i].occupied && !(slots_[i].key == k))
            i = (i + 1) & (N - 1);                      // linear probe
        slots_[i] = {true, k, v};
    }

    V* find(const K& k) {
        size_t i = std::hash<K>{}(k) & (N - 1);
        while (slots_[i].occupied) {
            if (slots_[i].key == k) return &slots_[i].value;
            i = (i + 1) & (N - 1);
        }
        return nullptr;                                  // not found
    }
};
```

**Why power-of-two capacity?**
`hash % N` requires integer division (~20-30 cycles). `hash & (N-1)` is a single AND instruction (~1 cycle). When N is a power of two, `N-1` is all-ones in binary, so `& (N-1)` gives the same result as `% N`.

> See [03_unordered_containers.cpp](file:///Users/arkaj/Desktop/Low-Latency-CPP/mini_quote_engine/cpp-high-performance/05-data-structures/code/03_unordered_containers.cpp) `section_open_addressing_bench()` — benchmarks LinearMap vs `std::unordered_map`.

---

## 5 — Container Adaptors (stack, queue, priority_queue)

### What Adaptors Are

Container adaptors **wrap** an underlying container and **restrict its API** to enforce a discipline (LIFO, FIFO, priority-ordered):

```cpp
template<class T, class Container = std::deque<T>>
class stack;       // push, pop, top — LIFO

template<class T, class Container = std::deque<T>>
class queue;       // push (back), pop (front), front — FIFO

template<class T, class Container = std::vector<T>,
         class Compare = std::less<typename Container::value_type>>
class priority_queue;   // push, pop, top — max-element first
```

### `std::stack` — Backend Matters

The default backend is `std::deque`, but **`std::vector` is almost always faster**:

```cpp
std::stack<int, std::vector<int>> s_vec;   // contiguous storage → cache-friendly
std::stack<int, std::deque<int>>  s_deq;   // chunked storage → cache-unfriendly
```

**Why vector wins for stack:**
A stack only needs `push_back` and `pop_back`. Vector does both in O(1) amortized with contiguous memory. Deque pays for its chunk-boundary bookkeeping on every access — overhead you never benefit from (since you never push_front on a stack).

> See [04_container_adaptors.cpp](file:///Users/arkaj/Desktop/Low-Latency-CPP/mini_quote_engine/cpp-high-performance/05-data-structures/code/04_container_adaptors.cpp) `section_stack_backends()`.

### `std::queue` — Why Deque is the Default

`std::queue` needs `push_back` AND `pop_front`. `std::vector` doesn't have an efficient `pop_front` (it's O(n) — you shift all elements left). `std::deque` does both in O(1). That's why deque is the default.

But for HFT: use a **ring buffer** instead. Deque allocates chunks from the heap; a ring buffer pre-allocates once.

### `std::priority_queue` — Binary Heap in a Vector

This is one of the most elegant data structures: a complete binary tree stored **implicitly** in a flat array:

```
  index:   0    1    2    3    4    5    6
  values: [50] [30] [40] [10] [20] [15] [25]

  Tree view:        50
                   /    \
                 30      40
                / \     / \
              10   20  15  25

  Index formulas:
    parent(i) = (i - 1) / 2
    left(i)   = 2i + 1
    right(i)  = 2i + 2
```

**Why this is cache-friendly:**
No pointers, no nodes, no heap allocations per element. The tree is a contiguous array — the CPU prefetcher handles it beautifully. Compare to a linked-list-based tree (like `std::set`) where each node is a random heap allocation.

**How `push_heap` works (sift up):**
```cpp
// Conceptual implementation:
void push_heap(vector<T>& v) {
    size_t i = v.size() - 1;           // last element = the new element
    while (i > 0) {
        size_t p = (i - 1) / 2;       // parent
        if (v[p] >= v[i]) break;       // heap property satisfied
        std::swap(v[p], v[i]);         // swap with parent
        i = p;
    }
}
// O(log n) — at most height-of-tree swaps
```

**How `pop_heap` works (sift down):**
```cpp
void pop_heap(vector<T>& v) {
    std::swap(v[0], v.back());         // move root to end
    size_t n = v.size() - 1;           // don't include the popped element
    size_t i = 0;
    while (true) {
        size_t l = 2*i + 1, r = 2*i + 2;
        size_t largest = i;
        if (l < n && v[l] > v[largest]) largest = l;
        if (r < n && v[r] > v[largest]) largest = r;
        if (largest == i) break;
        std::swap(v[i], v[largest]);
        i = largest;
    }
    // v.pop_back() to actually remove the old root (now at back)
}
```

### Priority Queue for Order Matching

```cpp
struct Order {
    double price;
    int    qty;
    int    time;       // arrival sequence — smaller = earlier
    bool   is_buy;
};

// Buy side: highest price first, then earliest time (price-time priority)
struct BuyOrderCmp {
    bool operator()(const Order& a, const Order& b) const noexcept {
        if (a.price != b.price) return a.price < b.price;   // higher price = higher prio
        return a.time > b.time;                               // earlier time = higher prio
    }
};

std::priority_queue<Order, std::vector<Order>, BuyOrderCmp> bids;
bids.push({100.0, 50, 1, true});
bids.push({101.0, 25, 2, true});
bids.push({100.5, 10, 3, true});

// bids.top() → the 101.0 order (highest price)
```

> See [04_container_adaptors.cpp](file:///Users/arkaj/Desktop/Low-Latency-CPP/mini_quote_engine/cpp-high-performance/05-data-structures/code/04_container_adaptors.cpp) `section_priority_queue()` and `section_heap_layout()`.

---

## 6 — Parallel Arrays — AoS vs SoA

### The Problem

When you have a collection of objects and your hot loop only touches ONE field, you're loading the entire struct into cache and wasting bandwidth on the fields you don't read.

### Array of Structures (AoS) — The OO-Natural Layout

```cpp
struct Order {
    double   price;    // 8 B
    int32_t  qty;      // 4 B
    uint64_t id;       // 8 B — aligned to 8 → 4 bytes of padding before it
    bool     is_buy;   // 1 B — + 7 bytes padding at end
};
// sizeof(Order) = 32 (with padding) or 24 (with compiler packing)
// Let's say 24 for this analysis.

std::vector<Order> orders(1'000'000);

// Hot loop: sum all prices
double sum = 0;
for (const auto& o : orders) sum += o.price;
// Each iteration loads 24 bytes (one Order), but only USES 8 bytes (the price).
// Waste: 16/24 = 67%
```

### Structure of Arrays (SoA) — The Performance Layout

```cpp
struct OrdersSoA {
    std::vector<double>   prices;    // dense array of just prices
    std::vector<int32_t>  qtys;
    std::vector<uint64_t> ids;
    std::vector<uint8_t>  is_buy;
};

OrdersSoA orders;
orders.prices.resize(1'000'000);
// ... fill all arrays ...

// Hot loop: sum all prices
double sum = 0;
for (double p : orders.prices) sum += p;
// Each iteration loads 8 bytes, uses 8 bytes. Waste: 0%.
```

### Cache-Line Math (1M orders, sum prices)

| Layout | Bytes per element | Useful bytes | Waste | Cache lines loaded (64 B each) |
|--------|-------------------|--------------|-------|-------------------------------|
| AoS | 24 | 8 (price) | 67% | 24M / 64 = **375,000** |
| SoA | 8 | 8 (price) | 0% | 8M / 64 = **125,000** |

**SoA loads 3× fewer cache lines.** But it gets even better:

### SIMD Vectorization — Why SoA Unlocks It

With SoA, the `prices` array is a contiguous `double[]`. The compiler can auto-vectorize:
- AVX: `vaddpd` processes **4 doubles per instruction** (256-bit registers).
- AVX-512: `vaddpd` processes **8 doubles per instruction**.

With AoS, the prices are at offsets 0, 24, 48, 72, ... — **non-contiguous stride**. The compiler can't use SIMD (or it generates expensive gather instructions).

**Real-world speedup: 5-20× for SoA on price-sum loops.**

```cpp
// Compile with -O3 -march=native to enable auto-vectorization
// Check with -fopt-info-vec-optimized (gcc) or -Rpass=loop-vectorize (clang)

double sum_soa(const std::vector<double>& prices) {
    double s = 0;
    for (double p : prices) s += p;    // ← compiler can vectorize this
    return s;
}
```

> See [05_soa_vs_aos.cpp](file:///Users/arkaj/Desktop/Low-Latency-CPP/mini_quote_engine/cpp-high-performance/05-data-structures/code/05_soa_vs_aos.cpp) for the full benchmark.

### The 80% Rule

> **If your hot loop reads ≥ 80% of the bytes of a struct → AoS is fine.**
> **If your hot loop reads < 80% → SoA wins, and the savings grow with the waste.**

**When AoS wins:**
If your hot loop touches *every* field — e.g., serializing each order to a FIX message — AoS puts all fields on the same cache line. SoA would require loading from 4 separate arrays (4 separate cache-line streams), which can be slower for small structs.

---

## 7 — Cache Lines, False Sharing, `alignas`

### What a Cache Line Is

The CPU doesn't read memory byte-by-byte. It reads in **cache lines** — fixed-size blocks:
- **x86 (Intel/AMD):** 64 bytes
- **Apple Silicon (M1/M2/M3):** 128 bytes
- **Some ARM (Cortex-A):** 64 bytes

When the CPU needs byte 100, it loads bytes 64-127 (the entire cache line containing byte 100) into L1 cache. This is the **spatial locality** principle: if you read byte 100, you'll probably read bytes 101-127 soon.

### The MESI Coherence Protocol

When a CPU core writes to a cache line, the hardware coherence protocol (MESI: Modified-Exclusive-Shared-Invalid) must ensure all other cores see the update:

1. Core A writes to byte 100 → the cache line (bytes 64-127) is marked **Modified** in Core A's L1.
2. If Core B also has this cache line cached, the protocol sends an **Invalidate** message to Core B.
3. Core B's copy is marked **Invalid**. Next time Core B reads any byte in 64-127, it must fetch the line from Core A's cache (or main memory) → **~50-100 ns penalty**.

### False Sharing — The Silent Performance Killer

False sharing occurs when two threads write to **different** variables that happen to sit on the **same** cache line. Neither thread touches the other's data, but the hardware treats the entire 64-byte line as one unit → constant invalidation ping-pong.

```cpp
// BAD — both atomics on the same cache line
struct BadCounters {
    std::atomic<uint64_t> producer_count;   // bytes 0-7
    std::atomic<uint64_t> consumer_count;   // bytes 8-15
    // Both within one 64-byte cache line!
};
// Thread 1 writes producer_count → invalidates Thread 2's cache line
// Thread 2 writes consumer_count → invalidates Thread 1's cache line
// Result: throughput drops 5-50×
```

```cpp
// GOOD — each atomic on its own cache line
struct GoodCounters {
    alignas(64) std::atomic<uint64_t> producer_count;   // bytes 0-63
    // 56 bytes of padding...
    alignas(64) std::atomic<uint64_t> consumer_count;   // bytes 64-127
};
// Thread 1 writes producer_count → no effect on Thread 2's cache line
// Thread 2 writes consumer_count → no effect on Thread 1's cache line
```

### `std::hardware_destructive_interference_size` (C++17)

```cpp
#include <new>
constexpr size_t CL = std::hardware_destructive_interference_size;
// x86: 64
// Apple M-series: 128

struct PortableCounters {
    alignas(CL) std::atomic<uint64_t> producer_count;
    alignas(CL) std::atomic<uint64_t> consumer_count;
};
```

> [!WARNING]
> On Apple Silicon, `hardware_destructive_interference_size` is 128, not 64. If you hardcode `alignas(64)`, you'll still false-share on M-series chips. Always use the constant, or benchmark on your target hardware.

### Benchmarking False Sharing

```cpp
#include <atomic>
#include <chrono>
#include <thread>
#include <iostream>

struct BadCounters {
    std::atomic<uint64_t> a{0};
    std::atomic<uint64_t> b{0};
};

struct GoodCounters {
    alignas(64) std::atomic<uint64_t> a{0};
    alignas(64) std::atomic<uint64_t> b{0};
};

template<class Counters>
double bench(const char* name) {
    Counters c;
    constexpr int N = 100'000'000;
    auto t0 = std::chrono::steady_clock::now();

    std::thread t1([&]{ for (int i = 0; i < N; ++i) c.a.fetch_add(1, std::memory_order_relaxed); });
    std::thread t2([&]{ for (int i = 0; i < N; ++i) c.b.fetch_add(1, std::memory_order_relaxed); });
    t1.join(); t2.join();

    auto t1_end = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration<double, std::milli>(t1_end - t0).count();
    std::cout << name << ": " << ms << " ms\n";
    return ms;
}

int main() {
    double bad  = bench<BadCounters>("BadCounters (false sharing)");
    double good = bench<GoodCounters>("GoodCounters (no false sharing)");
    std::cout << "Speedup: " << bad / good << "x\n";
    // Expect 5-50× speedup from eliminating false sharing
}
```

---

## 8 — Lock-Free SPSC Ring Buffer

### Why This Matters

The SPSC (Single-Producer, Single-Consumer) ring buffer is the **foundation of HFT inter-thread communication**. It's how you pass market data from the network thread to the strategy thread without locks, without system calls, and with single-digit nanosecond latency.

### Design — Power-of-Two Capacity

```
  head_ ──►  ┌───┬───┬───┬───┬───┬───┬───┬───┐
             │ X │ X │ X │ X │ . │ . │ . │ . │   buffer[N]  (N = 8 here)
  tail_ ──►  └───┴───┴───┴───┴───┴───┴───┴───┘
  Producer writes at head_, consumer reads at tail_
  Full  when head_ - tail_ == N
  Empty when head_ == tail_
```

**Why power of two?**
`index % N` requires integer division (~20-30 cycles). When N is a power of two, `index & (N-1)` gives the same result with a single AND instruction (~1 cycle). This matters when you're doing millions of operations per second.

### The Full Implementation

```cpp
template<class T, size_t N>
class SpscRing {
    static_assert((N & (N-1)) == 0, "N must be power of two");

    // CRITICAL: each on its own cache line to prevent false sharing
    alignas(64) std::atomic<size_t> head_{0};   // only producer writes
    alignas(64) std::atomic<size_t> tail_{0};   // only consumer writes
    alignas(64) std::array<T, N>    buf_;

public:
    // PRODUCER thread — only one thread calls this
    bool push(const T& v) noexcept {
        const auto h = head_.load(std::memory_order_relaxed);   // [1]
        const auto t = tail_.load(std::memory_order_acquire);   // [2]
        if (h - t == N) return false;                            // full
        buf_[h & (N-1)] = v;                                     // [3]
        head_.store(h + 1, std::memory_order_release);           // [4]
        return true;
    }

    // CONSUMER thread — only one thread calls this
    bool pop(T& v) noexcept {
        const auto t = tail_.load(std::memory_order_relaxed);    // [5]
        const auto h = head_.load(std::memory_order_acquire);    // [6]
        if (h == t) return false;                                 // empty
        v = buf_[t & (N-1)];                                     // [7]
        tail_.store(t + 1, std::memory_order_release);            // [8]
        return true;
    }
};
```

### Memory Orderings — WHY Each One

This is the most interview-critical part. Let's go line by line:

| Line | Operation | Ordering | Why |
|------|-----------|----------|-----|
| [1] | Producer reads `head_` | `relaxed` | Producer is the **only writer** of `head_`. Reading your own last write needs no synchronization — it's always visible to you. |
| [2] | Producer reads `tail_` | `acquire` | Producer must see the consumer's latest tail update to check "is the buffer full?". `acquire` ensures all writes the consumer did *before* its `release` store of `tail_` are visible. |
| [3] | Write to `buf_[h & (N-1)]` | (non-atomic) | Ordinary store — no atomic needed because only the producer writes to this slot, and the consumer won't read it until it sees `head_` increment. |
| [4] | Producer stores `head_` | `release` | **This is the publish point.** The `release` ensures that the write to `buf_` in [3] is visible to any thread that does an `acquire` load of `head_`. Without `release`, the compiler/CPU might reorder [3] after [4], and the consumer could read the slot before the producer wrote to it. |
| [5] | Consumer reads `tail_` | `relaxed` | Consumer is the **only writer** of `tail_`. Same reasoning as [1]. |
| [6] | Consumer reads `head_` | `acquire` | Consumer must see the producer's latest `head_` update. `acquire` pairs with the producer's `release` in [4]: it ensures the consumer sees the data written in [3] before the `head_` was incremented. |
| [7] | Read from `buf_[t & (N-1)]` | (non-atomic) | Only the consumer reads this slot (at this index), and the producer won't overwrite it until it sees `tail_` advance past it. |
| [8] | Consumer stores `tail_` | `release` | Publishes that the slot is now free. The producer will see this via its `acquire` load in [2]. |

**Why NOT `seq_cst`?**
`memory_order_seq_cst` provides a global total order across ALL atomic operations on ALL variables. It's the strongest (and slowest) ordering. On x86, `seq_cst` stores emit a `MFENCE` instruction (~20 ns); `release` stores are free (x86 has a strong memory model where all stores are release-ordered naturally). On ARM/RISC-V, `seq_cst` uses `DMB SY` (full barrier); `release` uses `DMB ISH` (lighter barrier).

For SPSC, we only have two synchronizing variables (`head_` and `tail_`), and we never need to reason about the ordering between them relative to a third variable. So `acquire/release` is the **minimum sufficient** ordering. Using `seq_cst` would work but would leave performance on the table.

### Cache-Line Discipline

```cpp
alignas(64) std::atomic<size_t> head_{0};   // cache line 0
alignas(64) std::atomic<size_t> tail_{0};   // cache line 1
alignas(64) std::array<T, N>    buf_;       // cache line 2+
```

Without `alignas(64)`, `head_` and `tail_` might share a cache line → false sharing between producer and consumer threads → latency explodes 5-50×.

> See [06_ringbuffer_spsc.cpp](file:///Users/arkaj/Desktop/Low-Latency-CPP/mini_quote_engine/cpp-high-performance/05-data-structures/code/06_ringbuffer_spsc.cpp) for the full implementation with a producer/consumer benchmark.

---

## 9 — Order-Book Data Structures

### What Is a Limit Order Book?

A limit-order book holds outstanding **limit orders** organized by price. Two sides:
- **Bids** (buy orders) — sorted by price **descending** (best bid = highest price).
- **Asks** (sell orders) — sorted by price **ascending** (best ask = lowest price).

At each price level, orders are in a **FIFO queue** (price-time priority: earlier orders get filled first).

```
             ASKS (sell side)                 BIDS (buy side)
             price       qty                  price       qty
             ─────────────                    ─────────────
  best ask → 101.5       100                  100.4       50  ← best bid
             101.6       200                  100.2       250
             101.7        20                  100.1        80
                                               99.5       10
```

**Spread** = best ask − best bid = 101.5 − 100.4 = 1.1.

### Design 1 — `std::map` (Textbook, Slow)

```cpp
struct Level { uint64_t qty; std::list<Order> orders; };

std::map<int, Level, std::greater<int>> bids_;   // descending — best at begin()
std::map<int, Level>                    asks_;   // ascending  — best at begin()
```

- `best_bid() = bids_.begin()->first` — O(1) since `begin()` is cached.
- `add_order(price, qty)` — O(log n) (tree insertion).
- `cancel_order(id)` — O(log n) to find the level, then O(n) to find the order in the list (unless you maintain a side `unordered_map<id, iterator>`).

**Why it's too slow for production HFT:**
A 1M-level book → log₂(1M) ≈ 20 cache misses per operation → ~1.4 μs per operation → at 10M messages/second, you can't keep up.

> See [07_orderbook_pricelevel.cpp](file:///Users/arkaj/Desktop/Low-Latency-CPP/mini_quote_engine/cpp-high-performance/05-data-structures/code/07_orderbook_pricelevel.cpp) `BookMap`.

### Design 2 — Sorted Vector + Binary Search

```cpp
struct Level { int price; uint64_t qty; };
std::vector<Level> bids_;   // sorted descending
std::vector<Level> asks_;   // sorted ascending
```

- `best_bid() = bids_.front()` — O(1).
- `find_level(price)` — binary search on contiguous memory → ~6 cache misses for 1M levels.
- `add_level(price)` — binary search to find position + O(n) shift to insert in the middle.

**When this wins:** read-heavy workloads (many lookups, few inserts/erases). When the number of active price levels is small (e.g., top-of-book only).

> See [07_orderbook_pricelevel.cpp](file:///Users/arkaj/Desktop/Low-Latency-CPP/mini_quote_engine/cpp-high-performance/05-data-structures/code/07_orderbook_pricelevel.cpp) `BookSortedVec`.

### Design 3 — Price-Indexed Array (Production HFT)

If the price range is bounded (e.g., ticks in [0, MAX_TICKS)):

```cpp
struct Level { uint64_t qty; /* intrusive list of orders */ };
Level levels_[MAX_TICKS];        // sparse array — most levels are empty
int best_bid_tick_ = 0;          // tracked as you add/remove
int best_ask_tick_ = MAX_TICKS;
```

- `add_order(price_tick, qty)` — **O(1)**: `levels_[price_tick].qty += qty`.
- `cancel_order(order_ptr)` — **O(1)**: unlink from the intrusive list.
- `best_bid()` — **O(1)**: `return best_bid_tick_`.
- `match()` — O(1) amortized: pop from `levels_[best_bid_tick_]`, then scan forward to find the next non-empty level.

**This is the layout every major HFT shop uses.**

```cpp
class BookArray {
    static constexpr int MAX_TICKS = 1'000'000;
    std::vector<int64_t> levels_;      // levels_[tick] = aggregate qty
    int best_bid_tick_ = 0;
    int best_ask_tick_ = 0;

public:
    BookArray() : levels_(MAX_TICKS, 0) {}

    void add(int price_tick, int64_t qty, bool is_buy) {
        levels_[price_tick] += qty;
        if (is_buy)
            best_bid_tick_ = std::max(best_bid_tick_, price_tick);
        else if (best_ask_tick_ == 0 || price_tick < best_ask_tick_)
            best_ask_tick_ = price_tick;
    }
    int best_bid() const { return best_bid_tick_; }
    int best_ask() const { return best_ask_tick_; }
};
```

> See [07_orderbook_pricelevel.cpp](file:///Users/arkaj/Desktop/Low-Latency-CPP/mini_quote_engine/cpp-high-performance/05-data-structures/code/07_orderbook_pricelevel.cpp) `BookArray`.

### Cancellations — Intrusive Doubly-Linked List

In a real order book, each price level has a queue of individual orders (FIFO). To cancel an order in O(1), you need to unlink it from the queue without searching:

```cpp
struct Order {
    Order* prev_ = nullptr;   // intrusive list pointers
    Order* next_ = nullptr;
    Level* level_;             // back-pointer to the containing Level
    uint64_t id;
    int64_t  qty;
};

struct Level {
    Order* head_ = nullptr;
    Order* tail_ = nullptr;
    int64_t total_qty = 0;

    void push_back(Order* o) {
        o->prev_ = tail_;
        o->next_ = nullptr;
        if (tail_) tail_->next_ = o;
        else head_ = o;
        tail_ = o;
        total_qty += o->qty;
    }

    void unlink(Order* o) {
        if (o->prev_) o->prev_->next_ = o->next_;
        else head_ = o->next_;
        if (o->next_) o->next_->prev_ = o->prev_;
        else tail_ = o->prev_;
        total_qty -= o->qty;
    }
};
```

**Cancel flow:**
1. Look up the order by ID: `flat_hash_map<OrderId, Order*> order_lookup_` → O(1).
2. Get the Level: `order->level_` → O(1).
3. Unlink: `level->unlink(order)` → O(1).
4. If Level is now empty, remove it from the price structure.

Total cancel latency: **O(1)**. This is critical — exchanges see 10-100× more cancels than fills.

---

## 10 — Allocators & Memory Pools

### Why Custom Allocators

Every STL container calls `std::allocator<T>::allocate()` which calls `::operator new` which calls `malloc`. In HFT:

- `malloc` can take a **lock** (contention with other threads → non-deterministic latency).
- `malloc` can trigger `mmap` (kernel mode switch → 1-10 μs).
- `malloc` can cause a **page fault** (TLB miss → page walk → 1-10 μs).

None of these are acceptable on the hot path. Solution: pre-allocate all memory and use a custom allocator that returns memory from a pre-allocated pool.

### `std::pmr` — Polymorphic Memory Resources (C++17)

```cpp
#include <memory_resource>
#include <unordered_map>
#include <array>

int main() {
    // 1 MB stack-allocated arena
    std::array<std::byte, 1 << 20> arena;

    // monotonic_buffer_resource: allocates sequentially from the arena
    // NEVER frees individual allocations — deallocate() is a no-op
    // Super fast: allocate() = bump a pointer
    std::pmr::monotonic_buffer_resource mbr{arena.data(), arena.size()};

    // Use it as the allocator for an unordered_map
    std::pmr::unordered_map<int, int> m{&mbr};

    // All inserts allocate from the stack arena — ZERO heap allocations
    for (int i = 0; i < 10000; ++i)
        m[i] = i;

    // When mbr goes out of scope, the arena is freed (it's on the stack).
    // No individual free() calls needed.
}
```

**Why `monotonic_buffer_resource` is perfect for short-lived containers:**
- `allocate()` = bump a pointer → O(1), no lock, no system call.
- `deallocate()` = no-op → O(1).
- Limitation: you can't reuse freed memory until the entire resource is destroyed. This is fine for containers that are built, used, and destroyed within a single function (e.g., building a snapshot of the order book).

### Pool Allocator — For Long-Lived Containers

When you need to allocate and free individual objects (not just build-and-destroy), you need a **pool allocator** with a **free list**:

```cpp
template<class T, size_t N>
class PoolAllocator {
    // Union trick: when the slot is free, it stores a pointer to the next free slot.
    // When the slot is occupied, it stores a T.
    union Slot {
        T value;
        Slot* next;
        Slot() {}    // default ctor does nothing — we'll placement-new when needed
        ~Slot() {}   // dtor does nothing — we'll explicitly destroy T when needed
    };

    alignas(T) std::byte storage_[N * sizeof(Slot)];
    Slot* free_list_;

public:
    PoolAllocator() {
        // Build the free list: each slot points to the next
        free_list_ = reinterpret_cast<Slot*>(storage_);
        for (size_t i = 0; i < N - 1; ++i)
            free_list_[i].next = &free_list_[i + 1];
        free_list_[N - 1].next = nullptr;
    }

    T* allocate() {
        if (!free_list_) throw std::bad_alloc{};
        Slot* s = free_list_;
        free_list_ = s->next;          // pop from free list — O(1)
        return reinterpret_cast<T*>(s);
    }

    void deallocate(T* p) {
        auto* s = reinterpret_cast<Slot*>(p);
        s->next = free_list_;          // push to free list — O(1)
        free_list_ = s;
    }
};
```

**Properties:**
- `allocate()` = O(1), branch-free, no system call, no lock.
- `deallocate()` = O(1), same.
- All memory is one contiguous block → great cache behavior.
- Fixed capacity N — can't grow (by design — deterministic).

**Usage in HFT:**
- Pool allocator for `Order` objects: pre-allocate 1M slots at startup.
- When a new order arrives: `allocate()` → O(1) from the pool.
- When an order is cancelled/filled: `deallocate()` → O(1) back to the pool.
- Zero heap allocations on the hot path.

---

## Summary — Key Takeaways

1. **Default to `std::vector`.** Contiguous, cache-friendly, amortized O(1) push, `reserve()` removes the amortization.
2. **`std::deque`, `std::list` are niche.** Reach for them only with a measured reason.
3. **`std::map` is O(log n) but slow** — red-black tree has poor locality. Consider `boost::flat_map` or `absl::btree_map`.
4. **`std::unordered_map` is slower than `absl::flat_hash_map`** because the standard mandates separate chaining.
5. **Iterator invalidation rules vary per container.** Know them cold.
6. **`alignas(64)` everything that's shared between threads** to avoid false sharing.
7. **Choose layout (AoS vs SoA) by the hot-loop access pattern**, not by what reads nicely.
8. **SPSC ring buffer** = the hot-path queue. `acquire/release` is the minimum sufficient ordering.
9. **For HFT order books: array indexed by price tick when possible**, sorted vector + binary search otherwise, `std::map` only for prototypes.
10. **Pre-allocate everything.** Custom allocators, `std::pmr`, pool allocators — zero `malloc` on the hot path.

---

## Connections to Previous Chapters

| Chapter | Connection |
|---|---|
| **Ch.1 — Brief Introduction** | Value semantics + RAII is WHY STL containers are safe — destructor handles cleanup, no manual delete. |
| **Ch.2 — Essential C++ Techniques** | `noexcept` move constructors → vector can move on resize instead of copy. Forwarding references → `emplace_back`. |
| **Ch.3 — Measuring Performance** | `ScopedTimer` from Chapter 3 is how you measure real container performance. Amortized O(1) is only amortized — your `perf` measurements prove it on real hardware. |

---

*Linked: [NOTES.md](file:///Users/arkaj/Desktop/Low-Latency-CPP/mini_quote_engine/cpp-high-performance/05-data-structures/NOTES.md) · [TODO.md](file:///Users/arkaj/Desktop/Low-Latency-CPP/mini_quote_engine/cpp-high-performance/05-data-structures/TODO.md) · [code/](file:///Users/arkaj/Desktop/Low-Latency-CPP/mini_quote_engine/cpp-high-performance/05-data-structures/code)*
