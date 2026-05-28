// 04_container_adaptors.cpp
// ---------------------------------------------------------------------
// Chapter 4 — Container adaptors (stack, queue, priority_queue)
//
// Build:  g++ -std=c++20 -O2 -Wall -Wextra 04_container_adaptors.cpp -o 04_adapt
//
// Demonstrates:
//   1. std::stack with vector backend vs deque backend.
//   2. std::priority_queue with a custom comparator — order-book matching.
//   3. The implicit binary-heap layout you can observe in the vector.
// ---------------------------------------------------------------------

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <iostream>
#include <queue>
#include <stack>
#include <string>
#include <vector>

// ---------- 1. stack: vector backend vs deque backend ----------
static void section_stack_backends() {
    std::cout << "=== std::stack: vector backend vs deque backend (10M push/pop) ===\n";
    constexpr int N = 10'000'000;

    auto bench = [](auto& s, const char* name) {
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < N; ++i) s.push(i);
        for (int i = 0; i < N; ++i) s.pop();
        auto t1 = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        std::cout << "  " << name << ": " << ms << " ms\n";
    };

    std::stack<int, std::vector<int>> s_vec;
    std::stack<int, std::deque<int>>  s_deq;
    bench(s_vec, "stack<int, vector<int>>");
    bench(s_deq, "stack<int, deque<int>>  ");
    std::cout << "  vector backend is usually faster — contiguous storage.\n\n";
}

// ---------- 2. priority_queue for order matching ----------
struct Order {
    double  price;
    int     qty;
    int     time;      // arrival sequence, smaller = earlier
    bool    is_buy;
};

// For a BUY priority queue: highest price first, then earliest time.
struct BuyOrderCmp {
    bool operator()(const Order& a, const Order& b) const noexcept {
        if (a.price != b.price) return a.price < b.price;   // higher price = higher prio
        return a.time > b.time;                              // earlier time = higher prio
    }
};

static void section_priority_queue() {
    std::cout << "=== priority_queue<Order, vector<Order>, BuyOrderCmp> ===\n";
    std::priority_queue<Order, std::vector<Order>, BuyOrderCmp> bids;
    bids.push({100.0, 50, 1, true});
    bids.push({101.0, 25, 2, true});
    bids.push({100.5, 10, 3, true});
    bids.push({101.0, 30, 4, true});       // same price, later time

    while (!bids.empty()) {
        const auto& o = bids.top();
        std::cout << "  POP: price=" << o.price << " qty=" << o.qty
                  << " time=" << o.time << '\n';
        bids.pop();
    }
    std::cout << "  Higher prices pop first; ties broken by earlier time.\n\n";
}

// ---------- 3. Binary heap layout in a vector ----------
static void section_heap_layout() {
    std::cout << "=== std::push_heap / std::pop_heap inside a vector ===\n";
    std::vector<int> v;
    for (int x : {3, 1, 4, 1, 5, 9, 2, 6, 5, 3}) {
        v.push_back(x);
        std::push_heap(v.begin(), v.end());     // sift up
    }
    std::cout << "  Heap array layout: ";
    for (int x : v) std::cout << x << ' ';
    std::cout << '\n';
    std::cout << "  Indices:           ";
    for (std::size_t i = 0; i < v.size(); ++i) std::cout << i << ' ';
    std::cout << "\n  (parent(i)=(i-1)/2, left=2i+1, right=2i+2)\n";

    std::cout << "  Pop order (max-heap): ";
    while (!v.empty()) {
        std::pop_heap(v.begin(), v.end());      // swap root with last, sift down
        std::cout << v.back() << ' ';
        v.pop_back();
    }
    std::cout << "\n\n";
}

int main() {
    section_stack_backends();
    section_priority_queue();
    section_heap_layout();
    return 0;
}
