// Differential test: every engine must produce exactly the same fills,
// cancel/modify results and resting book as the reference engine.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "lob/flat_book.hpp"
#include "lob/map_book.hpp"
#include "lob/order_gen.hpp"
#include "lob/reference_book.hpp"

using namespace lob;

template <class Book>
Book make_book(int) { return Book{}; }
template <>
FlatBook make_book<FlatBook>(int n_ops) {
    return FlatBook(2048, static_cast<uint32_t>(std::max(64, n_ops)));
}

template <class Book>
static bool run_sequence(uint64_t seed, int n_ops, Price spread, uint64_t& ops_done, uint64_t& fills) {
    ReferenceBook ref;
    Book book = make_book<Book>(n_ops);
    OrderGen gen(seed, 1000, spread);
    std::vector<Fill> fa, fb;
    for (int i = 0; i < n_ops; ++i) {
        Op op = gen.next();
        fa.clear();
        fb.clear();
        bool ra = apply(ref, op, fa);
        bool rb = apply(book, op, fb);
        ++ops_done;
        fills += fa.size();
        if (ra != rb || fa != fb) {
            std::printf("mismatch: seed %llu op %d (type %d, id %llu)\n",
                        (unsigned long long)seed, i, op.type, (unsigned long long)op.id);
            return false;
        }
    }
    if (ref.snapshot() != book.snapshot()) {
        std::printf("book mismatch at end of seed %llu\n", (unsigned long long)seed);
        return false;
    }
    return true;
}

template <class Book>
static bool suite(const char* name, uint64_t short_seqs, uint64_t long_seqs) {
    uint64_t ops = 0, fills = 0;
    for (uint64_t s = 0; s < short_seqs; ++s) {
        int len = 1 + static_cast<int>((s * 2654435761ULL) % 40);
        Price spread = 1 + static_cast<Price>(s % 6);  // tight books, lots of crossing
        if (!run_sequence<Book>(s, len, spread, ops, fills)) return false;
    }
    for (uint64_t s = 0; s < long_seqs; ++s)
        if (!run_sequence<Book>(1'000'000'007ULL + s, 20'000, 30, ops, fills)) return false;
    std::printf("%s: %llu sequences, %llu ops, %llu fills, all match the reference\n", name,
                (unsigned long long)(short_seqs + long_seqs), (unsigned long long)ops,
                (unsigned long long)fills);
    return true;
}

int main(int argc, char** argv) {
    uint64_t short_seqs = argc > 1 ? std::stoull(argv[1]) : 100'000;
    uint64_t long_seqs = argc > 2 ? std::stoull(argv[2]) : 20;
    bool ok = suite<MapBook>("MapBook", short_seqs, long_seqs) &&
              suite<FlatBook>("FlatBook", short_seqs, long_seqs);
    return ok ? 0 : 1;
}
