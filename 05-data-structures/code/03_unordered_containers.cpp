// 03_unordered_containers.cpp
// ---------------------------------------------------------------------
// Chapter 4 — Unordered associative containers (hash tables)
//
// Build:  g++ -std=c++20 -O2 -Wall -Wextra 03_unordered_containers.cpp -o 03_uo
//
// Demonstrates:
//   1. bucket_count(), load_factor(), max_load_factor() — internals.
//   2. reserve() to pre-allocate and avoid rehashes.
//   3. Custom hash for a user struct (OrderId).
//   4. A naive open-addressing linear-probing hash map (LinearMap)
//      compared against std::unordered_map.
// ---------------------------------------------------------------------

#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

// ---------- 1. Internals: bucket_count, load_factor, rehash ----------
static void section_internals() {
    std::cout << "=== std::unordered_map internals ===\n";
    std::unordered_map<int,int> m;
    std::cout << "  Empty: bucket_count=" << m.bucket_count()
              << " load_factor=" << m.load_factor()
              << " max_load_factor=" << m.max_load_factor() << "\n";
    for (int i = 0; i < 1000; ++i) m[i] = i;
    std::cout << "  After 1k inserts: bucket_count=" << m.bucket_count()
              << " load_factor=" << m.load_factor() << "\n\n";
}

// ---------- 2. reserve() avoids rehashes ----------
static void section_reserve() {
    std::cout << "=== reserve() removes amortization ===\n";
    {
        std::unordered_map<int,int> m;
        std::size_t prev = m.bucket_count();
        int rehashes = 0;
        for (int i = 0; i < 1'000'000; ++i) {
            m[i] = i;
            if (m.bucket_count() != prev) { ++rehashes; prev = m.bucket_count(); }
        }
        std::cout << "  Without reserve: " << rehashes << " rehashes\n";
    }
    {
        std::unordered_map<int,int> m;
        m.reserve(1'000'000);
        std::size_t prev = m.bucket_count();
        int rehashes = 0;
        for (int i = 0; i < 1'000'000; ++i) {
            m[i] = i;
            if (m.bucket_count() != prev) { ++rehashes; prev = m.bucket_count(); }
        }
        std::cout << "  With reserve(1M): " << rehashes << " rehashes  (should be 0)\n\n";
    }
}

// ---------- 3. Custom hash for a struct ----------
struct OrderId {
    std::uint64_t client_id;
    std::uint32_t local_id;
    bool operator==(const OrderId&) const noexcept = default;
};

struct OrderIdHash {
    std::size_t operator()(const OrderId& o) const noexcept {
        // splitmix64-style finalizer
        std::uint64_t x = o.client_id ^ (std::uint64_t(o.local_id) << 32);
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
        x =  x ^ (x >> 31);
        return std::size_t(x);
    }
};

static void section_custom_hash() {
    std::cout << "=== unordered_map<OrderId, ...> with custom hash ===\n";
    std::unordered_map<OrderId, int, OrderIdHash> orders;
    orders[{1ULL, 42U}] = 100;
    orders[{1ULL, 43U}] = 200;
    orders[{2ULL, 42U}] = 300;
    std::cout << "  size=" << orders.size()
              << " bucket_count=" << orders.bucket_count() << "\n\n";
}

// ---------- 4. Hand-rolled open-addressing linear-probing map ----------
template<class K, class V, std::size_t N>
class LinearMap {
    static_assert((N & (N-1)) == 0, "N must be power of two");
    struct Slot { bool occupied; K key; V value; };
    std::array<Slot, N> slots_{};
public:
    LinearMap() { for (auto& s : slots_) s.occupied = false; }

    void insert(const K& k, const V& v) {
        std::size_t i = std::hash<K>{}(k) & (N - 1);
        while (slots_[i].occupied && !(slots_[i].key == k)) i = (i + 1) & (N - 1);
        slots_[i] = {true, k, v};
    }
    V* find(const K& k) {
        std::size_t i = std::hash<K>{}(k) & (N - 1);
        while (slots_[i].occupied) {
            if (slots_[i].key == k) return &slots_[i].value;
            i = (i + 1) & (N - 1);
        }
        return nullptr;
    }
};

static void section_open_addressing_bench() {
    std::cout << "=== Open-addressing vs std::unordered_map (1M lookups) ===\n";
    constexpr std::size_t N = 1 << 20;       // 1M slots
    LinearMap<int, int, N> lm;
    std::unordered_map<int, int> um;
    um.reserve(N / 2);

    std::mt19937 rng(123);
    std::uniform_int_distribution<int> dist(0, int(N) - 1);
    for (std::size_t i = 0; i < N / 2; ++i) {
        int k = dist(rng);
        lm.insert(k, int(i));
        um[k] = int(i);
    }

    std::vector<int> queries(1'000'000);
    for (auto& q : queries) q = dist(rng);

    auto t0 = std::chrono::steady_clock::now();
    std::int64_t hits_lm = 0;
    for (int q : queries) if (lm.find(q)) ++hits_lm;
    auto t1 = std::chrono::steady_clock::now();
    std::int64_t hits_um = 0;
    for (int q : queries) if (um.find(q) != um.end()) ++hits_um;
    auto t2 = std::chrono::steady_clock::now();

    auto ms_lm = std::chrono::duration<double, std::milli>(t1 - t0).count();
    auto ms_um = std::chrono::duration<double, std::milli>(t2 - t1).count();
    std::cout << "  LinearMap (open addr)   : " << ms_lm << " ms   hits=" << hits_lm << '\n';
    std::cout << "  std::unordered_map       : " << ms_um << " ms   hits=" << hits_um << '\n';
    std::cout << "  Open addressing typically wins by 2-5x in hot paths.\n\n";
}

int main() {
    section_internals();
    section_reserve();
    section_custom_hash();
    section_open_addressing_bench();
    return 0;
}
