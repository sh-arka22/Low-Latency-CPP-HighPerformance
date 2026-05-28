// 01_sequence_containers.cpp
// ---------------------------------------------------------------------
// Chapter 4 — Sequence containers (vector, array, deque, list)
//
// Build:  g++ -std=c++20 -O2 -Wall -Wextra 01_sequence_containers.cpp -o 01_seq
// Run:    ./01_seq
//
// This file demonstrates:
//   1. sizeof of each sequence container (the 3-pointer layout of vector,
//      the chunked layout of deque, the linked layout of list).
//   2. The growth strategy of std::vector — verify the factor your STL uses.
//   3. Iterator invalidation under push_back (uncomment the trap line to
//      see what an unsanitized program would do).
//   4. A naive "FixedVector" (static_vector) for HFT hot paths — no heap.
// ---------------------------------------------------------------------

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iostream>
#include <list>
#include <vector>

// ---------- 1. sizeof of each container ----------
static void section_sizeof() {
    std::cout << "=== sizeof of empty containers (64-bit system) ===\n";
    std::cout << "sizeof(vector<int>)      = " << sizeof(std::vector<int>)     << "  // 3 ptrs\n";
    std::cout << "sizeof(deque<int>)       = " << sizeof(std::deque<int>)      << "  // map + iters\n";
    std::cout << "sizeof(list<int>)        = " << sizeof(std::list<int>)       << "  // sentinel + size\n";
    std::cout << "sizeof(array<int,16>)    = " << sizeof(std::array<int,16>)   << "  // pure storage\n";
    std::cout << '\n';
}

// ---------- 2. std::vector growth factor ----------
static void section_vector_growth() {
    std::cout << "=== std::vector capacity growth (1M push_backs) ===\n";
    std::vector<int> v;
    std::size_t last_cap = v.capacity();
    int growths = 0;
    for (int i = 0; i < 1'000'000; ++i) {
        v.push_back(i);
        if (v.capacity() != last_cap) {
            std::cout << "  cap " << last_cap << " -> " << v.capacity()
                      << "  (ratio " << double(v.capacity()) / std::max<std::size_t>(last_cap,1)
                      << ")\n";
            last_cap = v.capacity();
            ++growths;
        }
    }
    std::cout << "  Total growths: " << growths << "\n\n";
}

// ---------- 3. Iterator invalidation ----------
static void section_iterator_invalidation() {
    std::cout << "=== Iterator invalidation under push_back ===\n";
    std::vector<int> v{1, 2, 3};
    v.reserve(3);                // capacity == 3 → next push_back reallocates
    int* p = &v.front();         // pointer to first element BEFORE the realloc
    std::cout << "  *p before push_back: " << *p << '\n';

    // *** UNCOMMENT to trigger dangling pointer (run under -fsanitize=address)
    // v.push_back(4);
    // std::cout << "  *p after push_back: " << *p << "  // DANGLING!\n";

    std::cout << "  (uncomment the push_back line above and rerun with"
                 " -fsanitize=address to see the bug)\n\n";
}

// ---------- 4. FixedVector — stack-only, HFT-friendly ----------
template<class T, std::size_t N>
class FixedVector {
    alignas(T) std::byte storage_[sizeof(T) * N];
    std::size_t size_ = 0;
public:
    T*       data()       noexcept { return reinterpret_cast<T*>(storage_); }
    const T* data() const noexcept { return reinterpret_cast<const T*>(storage_); }
    std::size_t size() const noexcept { return size_; }
    static constexpr std::size_t capacity() noexcept { return N; }

    bool push_back(const T& v) {
        if (size_ == N) return false;        // bounded — never throws bad_alloc
        new (data() + size_) T(v);
        ++size_;
        return true;
    }
    void pop_back() {
        if (size_ == 0) return;
        --size_;
        data()[size_].~T();
    }
    T& operator[](std::size_t i) { return data()[i]; }
    ~FixedVector() { for (std::size_t i = 0; i < size_; ++i) data()[i].~T(); }
};

static void section_fixed_vector() {
    std::cout << "=== FixedVector<int,1024> — stack-only HFT container ===\n";
    FixedVector<int, 1024> fv;
    for (int i = 0; i < 1024; ++i) fv.push_back(i);
    std::cout << "  size = " << fv.size() << ", capacity = " << fv.capacity() << '\n';
    std::cout << "  zero heap allocations (verify with valgrind / perf)\n";
    std::cout << "  sizeof(FixedVector<int,1024>) = " << sizeof(fv) << " bytes\n\n";
}

// ---------- 5. Bench: vector vs list traversal ----------
static void section_traversal_bench() {
    std::cout << "=== Traversal benchmark: vector vs list (10M ints) ===\n";
    const int N = 10'000'000;
    std::vector<int> v(N, 1);
    std::list<int>   l(N, 1);

    auto t0 = std::chrono::steady_clock::now();
    std::int64_t sv = 0;
    for (auto x : v) sv += x;
    auto t1 = std::chrono::steady_clock::now();
    std::int64_t sl = 0;
    for (auto x : l) sl += x;
    auto t2 = std::chrono::steady_clock::now();

    auto ms_v = std::chrono::duration<double, std::milli>(t1 - t0).count();
    auto ms_l = std::chrono::duration<double, std::milli>(t2 - t1).count();
    std::cout << "  vector traversal: " << ms_v << " ms   (sum=" << sv << ")\n";
    std::cout << "  list   traversal: " << ms_l << " ms   (sum=" << sl << ")\n";
    std::cout << "  Ratio list/vector ≈ " << (ms_l / ms_v) << "x  (expect 5-20x)\n\n";
}

int main() {
    section_sizeof();
    section_vector_growth();
    section_iterator_invalidation();
    section_fixed_vector();
    section_traversal_bench();
    return 0;
}
