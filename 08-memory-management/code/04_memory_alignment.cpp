// Chapter 7 — Memory Management
// Topic 4: Memory Alignment and Padding
// Compile: g++ -std=c++20 -O2 -o 04 04_memory_alignment.cpp
// Run:     ./04

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <new>     // hardware_destructive_interference_size

// ─── 1. Struct padding demo ───────────────────────────────────────────────────

// Worst layout: largest wasted space
struct Bad {
    char    flag;    // 1 byte,  align 1, offset 0
                     // [3 bytes padding: next must be at multiple of 4]
    int32_t price;   // 4 bytes, align 4, offset 4
    char    side;    // 1 byte,  align 1, offset 8
                     // [7 bytes padding: sizeof must be multiple of 8]
    int64_t qty;     // 8 bytes, align 8, offset 16
};
// sizeof(Bad) = 24, but only 14 bytes used — 10 bytes wasted (42%)

// Better: sort fields largest-alignment first
struct Good {
    int64_t qty;     // 8 bytes, align 8, offset 0
    int32_t price;   // 4 bytes, align 4, offset 8
    char    flag;    // 1 byte,  align 1, offset 12
    char    side;    // 1 byte,  align 1, offset 13
                     // [2 bytes padding]
};
// sizeof(Good) = 16, 14 bytes used — 2 bytes wasted (12.5%)

// Optimal: zero padding
struct Optimal {
    int64_t qty;     // 8 bytes, align 8, offset 0
    int32_t price;   // 4 bytes, align 4, offset 8
    int16_t flags;   // 2 bytes, align 2, offset 12
    int8_t  flag;    // 1 byte,  align 1, offset 14
    int8_t  side;    // 1 byte,  align 1, offset 15
};
// sizeof(Optimal) = 16, 16 bytes used — 0 bytes wasted

static_assert(sizeof(Bad)     == 24);
static_assert(sizeof(Good)    == 16);
static_assert(sizeof(Optimal) == 16);

void show_padding() {
    std::cout << "--- Struct Padding ---\n";
    std::cout << "sizeof(Bad)     = " << sizeof(Bad)     << " (10 bytes wasted)\n";
    std::cout << "sizeof(Good)    = " << sizeof(Good)    << " (2 bytes wasted)\n";
    std::cout << "sizeof(Optimal) = " << sizeof(Optimal) << " (0 bytes wasted)\n";

    // Show offsets
    std::cout << "\nBad offsets:\n";
    std::cout << "  flag:  " << offsetof(Bad, flag)  << "\n";
    std::cout << "  price: " << offsetof(Bad, price) << "\n";
    std::cout << "  side:  " << offsetof(Bad, side)  << "\n";
    std::cout << "  qty:   " << offsetof(Bad, qty)   << "\n";

    std::cout << "\nGood offsets:\n";
    std::cout << "  qty:   " << offsetof(Good, qty)   << "\n";
    std::cout << "  price: " << offsetof(Good, price) << "\n";
    std::cout << "  flag:  " << offsetof(Good, flag)  << "\n";
    std::cout << "  side:  " << offsetof(Good, side)  << "\n\n";
}

// ─── 2. alignas — forcing alignment ──────────────────────────────────────────

// Cache-line aligned struct: lives on its own cache line
struct alignas(64) HotCounter {
    uint64_t value{0};
    char _pad[56];   // explicit padding to fill 64 bytes
};

// Check at compile time:
static_assert(sizeof(HotCounter) == 64);
static_assert(alignof(HotCounter) == 64);

void show_alignas() {
    std::cout << "--- alignas(64) ---\n";
    HotCounter a, b;  // consecutive on the stack
    std::cout << "sizeof(HotCounter) = " << sizeof(HotCounter) << "\n";
    std::cout << "&a = " << &a << "  (addr % 64 = "
              << reinterpret_cast<uintptr_t>(&a) % 64 << ")\n";
    std::cout << "&b = " << &b << "  (addr % 64 = "
              << reinterpret_cast<uintptr_t>(&b) % 64 << ")\n";
    std::cout << "a and b are on different cache lines: "
              << (reinterpret_cast<uintptr_t>(&b) - reinterpret_cast<uintptr_t>(&a) >= 64 ? "YES" : "NO")
              << "\n\n";
}

// ─── 3. hardware_destructive_interference_size ────────────────────────────────

void show_hardware_interference() {
    std::cout << "--- hardware_destructive_interference_size ---\n";
#ifdef __cpp_lib_hardware_interference_size
    std::cout << "hardware_destructive_interference_size = "
              << std::hardware_destructive_interference_size << "\n";
    std::cout << "hardware_constructive_interference_size = "
              << std::hardware_constructive_interference_size << "\n";
#else
    std::cout << "Not available on this platform (C++17 feature).\n";
    std::cout << "Fallback: use 64 on x86, 128 on Apple Silicon.\n";
#endif
    std::cout << "\n";
}

// ─── 4. HFT Order struct — tight layout with hot/cold separation ──────────────

struct alignas(64) HftOrder {
    // HOT: first 32 bytes — read every tick
    int64_t  price;        // offset 0
    int32_t  qty;          // offset 8
    int32_t  order_id;     // offset 12
    uint64_t symbol_hash;  // offset 16
    int32_t  side;         // offset 24
    int32_t  flags;        // offset 28
    // COLD: second 32 bytes — written at creation/cancel only
    uint64_t timestamp_ns; // offset 32
    uint64_t seq_num;      // offset 40
    char     symbol[12];   // offset 48
    int32_t  venue_id;     // offset 60
};

static_assert(sizeof(HftOrder) == 64, "Order must fit in one cache line");
static_assert(alignof(HftOrder) == 64, "Order must be cache-line aligned");

void show_hft_order() {
    std::cout << "--- HFT Order Layout ---\n";
    std::cout << "sizeof(HftOrder)  = " << sizeof(HftOrder) << " bytes (1 cache line)\n";
    std::cout << "  [HOT]  price        @ offset " << offsetof(HftOrder, price)      << "\n";
    std::cout << "  [HOT]  qty          @ offset " << offsetof(HftOrder, qty)        << "\n";
    std::cout << "  [HOT]  order_id     @ offset " << offsetof(HftOrder, order_id)   << "\n";
    std::cout << "  [HOT]  symbol_hash  @ offset " << offsetof(HftOrder, symbol_hash)<< "\n";
    std::cout << "  [HOT]  side         @ offset " << offsetof(HftOrder, side)       << "\n";
    std::cout << "  [HOT]  flags        @ offset " << offsetof(HftOrder, flags)      << "\n";
    std::cout << "  [COLD] timestamp_ns @ offset " << offsetof(HftOrder, timestamp_ns)<< "\n";
    std::cout << "  [COLD] seq_num      @ offset " << offsetof(HftOrder, seq_num)    << "\n";
    std::cout << "  [COLD] symbol       @ offset " << offsetof(HftOrder, symbol)     << "\n";
    std::cout << "  [COLD] venue_id     @ offset " << offsetof(HftOrder, venue_id)   << "\n";
    std::cout << "Reading hot fields never loads cold bytes → half the cache pressure.\n\n";
}

// ─── 5. Aligned allocation ────────────────────────────────────────────────────

// void show_aligned_alloc() {
//     std::cout << "--- aligned_alloc ---\n";

//     // C++17: aligned_alloc
//     void* p = std::aligned_alloc(64, 4096);
//     std::cout << "64-byte-aligned alloc at: " << p
//               << " (addr % 64 = " << reinterpret_cast<uintptr_t>(p) % 64 << ")\n";
//     std::free(p);

//     // C++17: ::operator new with alignment
//     void* q = ::operator new(4096, std::align_val_t{64});
//     std::cout << "operator new(64): at "
//               << q << " (addr % 64 = " << reinterpret_cast<uintptr_t>(q) % 64 << ")\n";
//     ::operator delete(q, std::align_val_t{64});
//     std::cout << "\n";
// }

// ─── 6. assume_aligned (C++20) ───────────────────────────────────────────────

// Hints to the compiler: the pointer is aligned to N bytes.
// This allows the autovectorizer to emit unmasked SIMD loads.

void process_aligned(float* data, int n) {
    // Without this hint, compiler generates a scalar loop or masked vector loop.
    // With this hint, it knows data is 32-byte aligned → AVX2 vmovaps (aligned load).
    //
    // C++20 std::assume_aligned (GCC 10+, Clang 10+):
#ifdef __cpp_lib_assume_aligned
    float* p = std::assume_aligned<32>(data);
#else
    // Fallback: GCC/Clang built-in (same effect, pre-C++20 or older stdlib)
    float* p = static_cast<float*>(__builtin_assume_aligned(data, 32));
#endif
    float sum = 0;
    for (int i = 0; i < n; ++i) sum += p[i];
    asm volatile("" : : "r,m"(sum) : "memory");
}

void show_assume_aligned() {
    std::cout << "--- std::assume_aligned (C++20) ---\n";
    alignas(32) float buf[256]{};
    process_aligned(buf, 256);
    std::cout << "32-byte aligned buffer processed with assume_aligned<32> hint.\n";
    std::cout << "Check disassembly: expect vmovaps (aligned) vs vmovdqu (unaligned).\n\n";
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "=== Memory Alignment and Padding ===\n\n";
    show_padding();
    show_alignas();
    show_hardware_interference();
    show_hft_order();
    // show_aligned_alloc();
    show_assume_aligned();

    std::cout << "=== Rules ===\n"
              << "  1. Sort struct fields largest-to-smallest alignof to minimise padding.\n"
              << "  2. Use alignas(64) on hot shared-state to prevent false sharing.\n"
              << "  3. Use hardware_destructive_interference_size, not hardcoded 64.\n"
              << "  4. Use std::assume_aligned to unlock autovectorisation.\n";
    return 0;
}
