# C++ High Performance

> Notes, concepts, and code examples from [Educative: C++ High Performance](https://www.educative.io/courses/c-plus-plus-high-performance)

## 📖 Course Overview

Gain insights into performance analysis, memory management, concurrency handling, and advanced templates in C++. Learn to write efficient, high-performance code and enhance your career opportunities.

## 🗂️ Repository Structure

Each chapter contains:
- `README.md` — Key concepts, notes, and summaries
- `code/` — Working code examples and exercises

## 📚 Table of Contents

| # | Chapter | Topics |
|---|---------|--------|
| 01 | [Getting Started](./01-getting-started/) | Course intro, Google Test & Benchmark setup |
| 02 | [A Brief Introduction to C++](./02-brief-introduction-to-cpp/) | Why C++, language features, comparisons |
| 03 | [Essential C++ Techniques](./03-essential-cpp-techniques/) | Move semantics, RAII, error handling, lambdas |
| 04 | [Analyzing and Measuring Performance](./04-analyzing-and-measuring-performance/) | Profiling, benchmarking, Big-O, cache effects |
| 05 | [Data Structures](./05-data-structures/) | STL containers, custom allocators, cache-friendly design |
| 06 | [Algorithms](./06-algorithms/) | STL algorithms, sorting, searching, complexity |
| 07 | [Ranges and Views](./07-ranges-and-views/) | C++20 ranges, views, pipelines, lazy evaluation |
| 08 | [Memory Management](./08-memory-management/) | Stack vs heap, smart pointers, custom allocators, alignment |
| 09 | [Compile-Time Programming](./09-compile-time-programming/) | constexpr, templates, metaprogramming, concepts |
| 10 | [Essential Utilities](./10-essential-utilities/) | std::optional, std::variant, std::any, type erasure |
| 11 | [Proxy Objects and Lazy Evaluation](./11-proxy-objects-and-lazy-evaluation/) | Expression templates, lazy evaluation patterns |
| 12 | [Concurrency](./12-concurrency/) | Threads, mutexes, atomics, lock-free programming |
| 13 | [Coroutines and Lazy Generators](./13-coroutines-and-lazy-generators/) | C++20 coroutines, generators, co_yield |
| 14 | [Asynchronous Programming with Coroutines](./14-asynchronous-programming-with-coroutines/) | co_await, async tasks, coroutine schedulers |
| 15 | [Parallel Algorithms](./15-parallel-algorithms/) | Execution policies, parallel STL, GPU offloading |

## 🛠️ Build & Run

```bash
# Compile a single example
g++ -std=c++20 -O2 -o example 01-getting-started/code/example.cpp

# With Google Benchmark (if installed)
g++ -std=c++20 -O2 -o bench example_bench.cpp -lbenchmark -lpthread
```

## 📋 Prerequisites

- C++ compiler with C++20 support (GCC 12+, Clang 14+, MSVC 2022+)
- Basic understanding of C++ syntax and OOP
- Google Test and Google Benchmark (optional, for running benchmarks)

## 📄 License

This repository is for personal learning and educational purposes.
