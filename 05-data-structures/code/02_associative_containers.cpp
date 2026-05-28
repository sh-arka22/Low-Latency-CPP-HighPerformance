// 02_associative_containers.cpp
// ---------------------------------------------------------------------
// Chapter 4 — Associative containers (std::map / std::set)
//
// Build:  g++ -std=c++20 -O2 -Wall -Wextra 02_associative_containers.cpp -o 02_assoc
//
// Demonstrates:
//   1. The red-black tree backing std::map (O(log n) ops, sorted iteration).
//   2. lower_bound / upper_bound — the price-ladder pattern.
//   3. Iterator stability across inserts/erases.
//   4. The operator[] default-construct trap.
//   5. Bench: std::map lookups vs sorted-vector binary search.
// ---------------------------------------------------------------------

#include <algorithm>
#include <chrono>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <vector>

// ---------- 1. Sorted iteration ----------
static void section_sorted_iteration() {
    std::cout << "=== std::map sorted iteration ===\n";
    std::map<int, std::string> m{
        {30, "thirty"}, {10, "ten"}, {20, "twenty"}, {5, "five"}
    };
    for (auto& [k, v] : m) std::cout << "  " << k << " -> " << v << '\n';
    std::cout << "  (note: iteration is sorted by key — the red-black tree invariant)\n\n";
}

// ---------- 2. lower_bound / upper_bound — price ladder ----------
static void section_price_ladder() {
    std::cout << "=== Price ladder with std::map<int, int> ===\n";
    std::map<int, int> book;
    book[10000] = 100;   // price=$100.00 (cents) qty=100
    book[10025] = 200;
    book[10050] = 50;
    book[10100] = 25;

    auto it = book.lower_bound(10030);
    std::cout << "  lower_bound(10030) -> price=" << it->first << " qty=" << it->second << '\n';

    auto first_too_high = book.upper_bound(10050);
    std::cout << "  upper_bound(10050) -> price=" << first_too_high->first << '\n';

    std::cout << "  Best price (begin): " << book.begin()->first
              << " qty=" << book.begin()->second << "\n\n";
}

// ---------- 3. Iterator stability ----------
static void section_iterator_stability() {
    std::cout << "=== std::map preserves iterators across other inserts ===\n";
    std::map<int, int> m{{1, 10}, {2, 20}, {3, 30}};
    auto it = m.find(2);
    std::cout << "  Before insert: it->second = " << it->second << '\n';
    m[100] = 1000;          // insert elsewhere
    m[200] = 2000;
    m.erase(3);             // erase a different key
    std::cout << "  After insert+erase: it->second = " << it->second
              << "   (iterator still valid)\n\n";
}

// ---------- 4. operator[] trap ----------
static void section_op_bracket_trap() {
    std::cout << "=== std::map operator[] default-constructs missing keys ===\n";
    std::map<std::string, int> counts;
    if (counts["never_seen"] == 0) {       // !! this INSERTED an entry
        std::cout << "  After if-check, size = " << counts.size() << "   (oops)\n";
    }
    std::cout << "  Safer: use .find() which does NOT insert.\n\n";
}

// ---------- 5. Bench: std::map vs sorted vector ----------
static void section_map_vs_sorted_vector() {
    std::cout << "=== std::map vs sorted vector — 1M lookups ===\n";
    constexpr int N = 100'000;
    std::vector<int> keys(N);
    for (int i = 0; i < N; ++i) keys[i] = i * 2;       // even ints

    std::map<int,int> m;
    for (int k : keys) m[k] = k;

    std::vector<std::pair<int,int>> sv;
    sv.reserve(N);
    for (int k : keys) sv.emplace_back(k, k);
    // Already sorted because we inserted in order.

    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, 2 * N - 1);
    std::vector<int> queries(1'000'000);
    for (auto& q : queries) q = dist(rng);

    // map lookups
    auto t0 = std::chrono::steady_clock::now();
    std::int64_t found_m = 0;
    for (int q : queries) if (m.find(q) != m.end()) ++found_m;
    auto t1 = std::chrono::steady_clock::now();

    // sorted-vector binary search
    std::int64_t found_v = 0;
    for (int q : queries) {
        auto it = std::lower_bound(sv.begin(), sv.end(),
                                   std::make_pair(q, 0),
                                   [](const auto& a, const auto& b){ return a.first < b.first; });
        if (it != sv.end() && it->first == q) ++found_v;
    }
    auto t2 = std::chrono::steady_clock::now();

    auto ms_m = std::chrono::duration<double, std::milli>(t1 - t0).count();
    auto ms_v = std::chrono::duration<double, std::milli>(t2 - t1).count();
    std::cout << "  std::map        : " << ms_m << " ms  (hits=" << found_m << ")\n";
    std::cout << "  sorted vector   : " << ms_v << " ms  (hits=" << found_v << ")\n";
    std::cout << "  Sorted vector typically wins by 2-5x on cache.\n\n";
}

int main() {
    section_sorted_iteration();
    section_price_ladder();
    section_iterator_stability();
    section_op_bracket_trap();
    section_map_vs_sorted_vector();
    return 0;
}
