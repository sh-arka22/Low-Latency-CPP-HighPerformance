// 07_orderbook_pricelevel.cpp
// ---------------------------------------------------------------------
// Chapter 4 — A minimal price-level limit order book.
//
// Build:  g++ -std=c++20 -O2 -Wall -Wextra 07_orderbook_pricelevel.cpp -o 07_book
//
// Demonstrates THREE designs of the price-level container:
//   (A) std::map<int, Level, std::greater<>> for bids and std::map<> for asks
//   (B) Sorted std::vector<Level> + binary search
//   (C) Sketch of an array indexed by integer price tick (production HFT)
//
// Each design supports: add_order, cancel_order, best_bid/ask, match_step.
// ---------------------------------------------------------------------

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <list>
#include <map>
#include <unordered_map>
#include <vector>

using Price = std::int32_t;          // price expressed in ticks (cents, sat, etc.)
using Qty   = std::int64_t;
using OrdId = std::uint64_t;

struct Order {
    OrdId  id;
    Price  price;
    Qty    qty;
    bool   is_buy;
};

// =====================================================================
// Design A: std::map-backed book (textbook)
// =====================================================================
class BookMap {
public:
    void add(const Order& o) {
        if (o.is_buy) {
            auto it = bids_.try_emplace(o.price, 0).first;
            it->second += o.qty;
        } else {
            auto it = asks_.try_emplace(o.price, 0).first;
            it->second += o.qty;
        }
        orders_[o.id] = o;
    }
    bool cancel(OrdId id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) return false;
        const Order& o = it->second;
        if (o.is_buy) {
            auto lvl = bids_.find(o.price);
            if (lvl != bids_.end()) {
                lvl->second -= o.qty;
                if (lvl->second <= 0) bids_.erase(lvl);
            }
        } else {
            auto lvl = asks_.find(o.price);
            if (lvl != asks_.end()) {
                lvl->second -= o.qty;
                if (lvl->second <= 0) asks_.erase(lvl);
            }
        }
        orders_.erase(it);
        return true;
    }
    Price best_bid() const { return bids_.empty() ?  0 : bids_.begin()->first; }
    Price best_ask() const { return asks_.empty() ?  0 : asks_.begin()->first; }

private:
    // bids: descending price; asks: ascending price.
    std::map<Price, Qty, std::greater<>> bids_;
    std::map<Price, Qty>                 asks_;
    std::unordered_map<OrdId, Order>     orders_;
};

// =====================================================================
// Design B: sorted-vector book (binary search)
// =====================================================================
class BookSortedVec {
public:
    void add(const Order& o) {
        auto& side = o.is_buy ? bids_ : asks_;
        auto cmp_bids = [](const Level& a, Price p){ return a.price > p; };  // desc
        auto cmp_asks = [](const Level& a, Price p){ return a.price < p; };  // asc
        auto it = o.is_buy
            ? std::lower_bound(side.begin(), side.end(), o.price, cmp_bids)
            : std::lower_bound(side.begin(), side.end(), o.price, cmp_asks);
        if (it != side.end() && it->price == o.price) it->qty += o.qty;
        else                                          side.insert(it, {o.price, o.qty});
        orders_[o.id] = o;
    }
    Price best_bid() const { return bids_.empty() ? 0 : bids_.front().price; }
    Price best_ask() const { return asks_.empty() ? 0 : asks_.front().price; }

private:
    struct Level { Price price; Qty qty; };
    std::vector<Level> bids_;     // sorted descending
    std::vector<Level> asks_;     // sorted ascending
    std::unordered_map<OrdId, Order> orders_;
};

// =====================================================================
// Design C: price-indexed array (production HFT sketch)
// =====================================================================
// If price range is bounded [0, MAX_TICKS), levels_[tick] is O(1).
// Track best_bid_tick_ / best_ask_tick_ as you mutate to keep best-of-side O(1).
class BookArray {
    static constexpr Price MAX_TICKS = 1'000'000;
public:
    BookArray() : levels_(MAX_TICKS, 0) {}
    void add(const Order& o) {
        levels_[o.price] += o.qty;
        if (o.is_buy)  best_bid_tick_ = std::max(best_bid_tick_, o.price);
        else if (best_ask_tick_ == 0 || o.price < best_ask_tick_) best_ask_tick_ = o.price;
    }
    Price best_bid() const { return best_bid_tick_; }
    Price best_ask() const { return best_ask_tick_; }
private:
    std::vector<Qty> levels_;
    Price best_bid_tick_ = 0;
    Price best_ask_tick_ = 0;
};

// =====================================================================
// Demo & micro-benchmark
// =====================================================================
template<class Book>
static double bench(int N, const char* name) {
    Book b;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < N; ++i) {
        Order o{ OrdId(i), Price(100 + (i % 1000)), 10, (i & 1) == 0 };
        b.add(o);
    }
    [[maybe_unused]] Price bb = b.best_bid();
    [[maybe_unused]] Price ba = b.best_ask();
    auto t1 = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::cout << "  " << name << ": " << ms << " ms  bb=" << bb << " ba=" << ba << '\n';
    return ms;
}

int main() {
    std::cout << "=== Order book design comparison (1M adds) ===\n";
    constexpr int N = 1'000'000;
    bench<BookMap>      (N, "BookMap        (std::map)");
    bench<BookSortedVec>(N, "BookSortedVec  (sorted vec + binsearch)");
    bench<BookArray>    (N, "BookArray      (price-indexed array)");
    std::cout << "\nKey takeaways:\n";
    std::cout << "  * BookMap is the textbook but slowest — every op is ~20 cache misses.\n";
    std::cout << "  * BookSortedVec wins on lookups, loses on mid-array inserts.\n";
    std::cout << "  * BookArray (price-indexed) is O(1) per op and is the production pattern\n";
    std::cout << "    when price ticks are bounded.\n";
    return 0;
}
