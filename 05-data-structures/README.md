# Chapter 4 — Data Structures

> Book: *C++ High Performance* (2nd ed.), Andrist & Sehr — Chapter 4
> Folder: `05-data-structures/`

## 📖 Topics covered

- Sequence containers — `vector`, `array`, `deque`, `list`, `forward_list`
- Associative containers — `map`, `set` (and their multi-variants)
- Unordered associative — `unordered_map`, `unordered_set` (+ open-addressing alternatives)
- Container adaptors — `stack`, `queue`, `priority_queue`
- Parallel arrays — **AoS vs SoA** for cache-friendly hot loops
- Cache-line alignment, `alignas(64)`, false sharing
- Lock-free **SPSC ring buffer**
- Order-book data structures (`std::map` vs sorted-vector vs price-indexed array)
- Allocators and memory pools (preview of Chapter 7)

## 📂 Files in this folder

| File | Purpose |
|------|---------|
| [`TODO.md`](./TODO.md) | Level-1/2/3 task list per topic with "interviewer asks" prompts |
| [`NOTES.md`](./NOTES.md) | BFS concept map + section-by-section deep notes |
| [`05_data_structures_deep_dive.md`](./05_data_structures_deep_dive.md) | HFT-grade internals — every container's memory layout + 25 interview questions |
| `code/01_sequence_containers.cpp` | sizeof, growth factor, iterator invalidation, FixedVector, vector-vs-list bench |
| `code/02_associative_containers.cpp` | sorted iteration, lower_bound price ladder, iterator stability, map-vs-sorted-vector |
| `code/03_unordered_containers.cpp` | bucket_count/load_factor, reserve, custom hash, open-addressing LinearMap |
| `code/04_container_adaptors.cpp` | stack backends, priority_queue with Order comparator, heap layout |
| `code/05_soa_vs_aos.cpp` | sum-of-prices benchmark: AoS vs SoA on 50M orders |
| `code/06_ringbuffer_spsc.cpp` | Lock-free single-producer/single-consumer ring buffer with cache-line alignment |
| `code/07_orderbook_pricelevel.cpp` | Three order-book designs: map, sorted vector, price-indexed array |

## 🛠️ Build

```bash
cd code
for f in *.cpp; do
    g++ -std=c++20 -O2 -Wall -Wextra -pthread "$f" -o "${f%.cpp}"
done
./01_sequence_containers
./05_soa_vs_aos          # use -O3 -march=native to see SIMD speedup
./06_ringbuffer_spsc
```

## 🔗 Linked repos

- [`PacktPublishing/Cpp-High-Performance-Second-Edition`](https://github.com/PacktPublishing/Cpp-High-Performance-Second-Edition) — book source code (Chapter 4)
- [`ITHelpDec/CPP-High-Performance`](https://github.com/ITHelpDec/CPP-High-Performance) — annotated walkthrough
- [`sh-arka22/Low-Latency-CPP-HighPerformance`](https://github.com/sh-arka22/Low-Latency-CPP-HighPerformance) — your fork
