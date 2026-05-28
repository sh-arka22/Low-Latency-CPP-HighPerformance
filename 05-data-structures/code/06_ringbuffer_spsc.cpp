// 06_ringbuffer_spsc.cpp
// ---------------------------------------------------------------------
// Chapter 4 — Lock-free single-producer single-consumer ring buffer.
//
// Build:  g++ -std=c++20 -O2 -Wall -Wextra -pthread 06_ringbuffer_spsc.cpp -o 06_spsc
//
// Demonstrates:
//   1. Power-of-two capacity → cheap modulo via bitmask.
//   2. Acquire/Release memory orderings — the minimum sufficient for SPSC.
//   3. Cache-line alignment (alignas) on head/tail to avoid false sharing.
//   4. A simple producer/consumer thread pair pushing 10M ints.
// ---------------------------------------------------------------------

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <new>            // hardware_destructive_interference_size
#include <thread>

#if defined(__cpp_lib_hardware_interference_size)
constexpr std::size_t CL = std::hardware_destructive_interference_size;
#else
constexpr std::size_t CL = 64;     // fallback for compilers that don't expose it
#endif

template<class T, std::size_t N>
class SpscRing {
    static_assert((N & (N - 1)) == 0, "N must be a power of two");

    // head_ and tail_ live on SEPARATE cache lines — no false sharing.
    alignas(CL) std::atomic<std::size_t> head_{0};   // producer writes
    alignas(CL) std::atomic<std::size_t> tail_{0};   // consumer reads
    alignas(CL) std::array<T, N>         buf_{};

public:
    // PRODUCER side (one thread).
    bool push(const T& v) noexcept {
        const auto h = head_.load(std::memory_order_relaxed);
        const auto t = tail_.load(std::memory_order_acquire);   // see consumer
        if (h - t == N) return false;                            // full
        buf_[h & (N - 1)] = v;
        head_.store(h + 1, std::memory_order_release);           // publish
        return true;
    }

    // CONSUMER side (one thread).
    bool pop(T& v) noexcept {
        const auto t = tail_.load(std::memory_order_relaxed);
        const auto h = head_.load(std::memory_order_acquire);   // see producer
        if (h == t) return false;                                // empty
        v = buf_[t & (N - 1)];
        tail_.store(t + 1, std::memory_order_release);           // publish
        return true;
    }

    std::size_t size() const noexcept {
        return head_.load(std::memory_order_acquire)
             - tail_.load(std::memory_order_acquire);
    }
};

int main() {
    std::cout << "=== SPSC ring buffer — 10M ints producer/consumer ===\n";
    std::cout << "hardware_destructive_interference_size = " << CL << " bytes\n\n";

    constexpr std::size_t N = 1 << 16;       // 64k slots
    constexpr std::int64_t M = 10'000'000;
    SpscRing<std::int64_t, N> ring;

    std::int64_t sum_consumed = 0;
    std::int64_t expected_sum = (M - 1) * M / 2;   // 0+1+...+(M-1)

    auto t0 = std::chrono::steady_clock::now();

    std::thread producer([&]{
        for (std::int64_t i = 0; i < M; ) {
            if (ring.push(i)) ++i;
            // else: spin until consumer makes room
        }
    });

    std::thread consumer([&]{
        std::int64_t v;
        std::int64_t count = 0;
        while (count < M) {
            if (ring.pop(v)) { sum_consumed += v; ++count; }
        }
    });

    producer.join();
    consumer.join();

    auto t1 = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    std::cout << "Pushed/popped: " << M << " items in " << ms << " ms\n";
    std::cout << "Throughput  : " << (M / (ms / 1000.0)) / 1e6 << " M items/s\n";
    std::cout << "Latency/op  : " << (ms * 1e6 / M) << " ns/op (avg, includes contention)\n";
    std::cout << "Sum check   : " << sum_consumed << " == " << expected_sum
              << "  " << (sum_consumed == expected_sum ? "OK" : "FAIL") << '\n';
    return 0;
}
