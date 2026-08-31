// Differential test: every engine must produce exactly the same fills,
// cancel/modify results and resting book as the reference engine.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "lob/map_book.hpp"
#include "lob/order_gen.hpp"
#include "lob/reference_book.hpp"

using namespace lob;

template <class Book>
static bool run_sequence(uint64_t seed, int n_ops, Price spread, uint64_t& ops_done) {
    ReferenceBook ref;
    Book book;
    OrderGen gen(seed, 1000, spread);
    std::vector<Fill> fa, fb;
    for (int i = 0; i < n_ops; ++i) {
        Op op = gen.next();
        fa.clear();
        fb.clear();
        bool ra = apply(ref, op, fa);
        bool rb = apply(book, op, fb);
        ++ops_done;
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
    uint64_t ops = 0;
    for (uint64_t s = 0; s < short_seqs; ++s) {
        int len = 1 + static_cast<int>((s * 2654435761ULL) % 40);
        Price spread = 1 + static_cast<Price>(s % 6);  // tight books, lots of crossing
        if (!run_sequence<Book>(s, len, spread, ops)) return false;
    }
    for (uint64_t s = 0; s < long_seqs; ++s)
        if (!run_sequence<Book>(1'000'000'007ULL + s, 20'000, 30, ops)) return false;
    std::printf("%s: %llu sequences, %llu ops, all match the reference\n", name,
                (unsigned long long)(short_seqs + long_seqs), (unsigned long long)ops);
    return true;
}

int main(int argc, char** argv) {
    uint64_t short_seqs = argc > 1 ? std::stoull(argv[1]) : 100'000;
    uint64_t long_seqs = argc > 2 ? std::stoull(argv[2]) : 20;
    bool ok = suite<MapBook>("MapBook", short_seqs, long_seqs);
    return ok ? 0 : 1;
}
