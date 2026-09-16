// Throughput and latency benchmark: MapBook (first version) vs FlatBook (optimized)
// on the same pre-generated order flow.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "lob/flat_book.hpp"
#include "lob/map_book.hpp"
#include "lob/order_gen.hpp"

using namespace lob;
using Clock = std::chrono::steady_clock;

template <class Book>
static double throughput(const std::vector<Op>& ops, Book book, uint64_t& fills) {
    std::vector<Fill> out;
    out.reserve(1024);
    auto t0 = Clock::now();
    for (const Op& op : ops) {
        out.clear();
        apply(book, op, out);
        fills += out.size();
    }
    std::chrono::duration<double> s = Clock::now() - t0;
    return ops.size() / s.count();
}

template <class Book>
static void latency(const char* name, const std::vector<Op>& ops, Book book) {
    std::vector<Fill> out;
    out.reserve(1024);
    std::vector<uint32_t> ns;
    ns.reserve(ops.size());
    for (const Op& op : ops) {
        out.clear();
        auto t0 = Clock::now();
        apply(book, op, out);
        ns.push_back(static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count()));
    }
    std::sort(ns.begin(), ns.end());
    auto p = [&](double q) { return ns[static_cast<size_t>((ns.size() - 1) * q)]; };
    std::printf("%-9s latency  p50 %u ns  p99 %u ns  p99.9 %u ns\n", name, p(0.5), p(0.99), p(0.999));
}

int main(int argc, char** argv) {
    size_t n = argc > 1 ? std::stoull(argv[1]) : 5'000'000;
    int runs = argc > 2 ? std::stoi(argv[2]) : 5;

    OrderGen gen(42, 10'000, 40);
    std::vector<Op> ops(n);
    for (auto& op : ops) op = gen.next();

    double best_map = 0, best_flat = 0;
    uint64_t fills = 0;
    for (int r = 0; r < runs; ++r) {
        fills = 0;
        best_map = std::max(best_map, throughput(ops, MapBook{}, fills));
        fills = 0;
        best_flat = std::max(best_flat, throughput(ops, FlatBook(1 << 15, 1 << 20), fills));
    }
    std::printf("orders: %zu  fills: %llu  best of %d runs\n", n, (unsigned long long)fills, runs);
    std::printf("MapBook   %.2fM ops/s\n", best_map / 1e6);
    std::printf("FlatBook  %.2fM ops/s  (%.0f%% higher)\n", best_flat / 1e6,
                (best_flat / best_map - 1) * 100);
    latency("MapBook", ops, MapBook{});
    latency("FlatBook", ops, FlatBook(1 << 15, 1 << 20));
}
