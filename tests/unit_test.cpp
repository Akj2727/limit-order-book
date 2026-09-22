// Hand-written scenarios for the matching rules, run against every engine.
#include <cstdio>
#include <vector>

#include "lob/flat_book.hpp"
#include "lob/map_book.hpp"
#include "lob/reference_book.hpp"

using namespace lob;

static int failures = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

template <class Book>
Book fresh() { return Book{}; }
template <>
FlatBook fresh<FlatBook>() { return FlatBook(1024, 256); }

template <class Book>
void partial_fill_rests_remainder() {
    Book b = fresh<Book>();
    std::vector<Fill> f;
    b.add_limit(1, Side::Sell, 100, 30, f);
    b.add_limit(2, Side::Buy, 100, 50, f);
    CHECK(f.size() == 1 && f[0] == (Fill{2, 1, 100, 30}));
    auto s = b.snapshot();
    CHECK(s.size() == 1 && s[0] == (Resting{Side::Buy, 100, 2, 20}));
}

template <class Book>
void price_then_time_priority() {
    Book b = fresh<Book>();
    std::vector<Fill> f;
    b.add_limit(1, Side::Sell, 101, 10, f);
    b.add_limit(2, Side::Sell, 100, 10, f);
    b.add_limit(3, Side::Sell, 100, 10, f);
    b.add_limit(4, Side::Buy, 101, 25, f);
    // Best price first (100), oldest first within it (2 then 3), then 101.
    CHECK(f.size() == 3);
    CHECK(f[0] == (Fill{4, 2, 100, 10}));
    CHECK(f[1] == (Fill{4, 3, 100, 10}));
    CHECK(f[2] == (Fill{4, 1, 101, 5}));
}

template <class Book>
void limit_does_not_trade_through_its_price() {
    Book b = fresh<Book>();
    std::vector<Fill> f;
    b.add_limit(1, Side::Buy, 99, 10, f);
    b.add_limit(2, Side::Sell, 100, 10, f);
    CHECK(f.empty());
    CHECK(b.order_count() == 2);
}

template <class Book>
void market_sweeps_levels_and_drops_rest() {
    Book b = fresh<Book>();
    std::vector<Fill> f;
    b.add_limit(1, Side::Buy, 100, 5, f);
    b.add_limit(2, Side::Buy, 98, 5, f);
    b.market(3, Side::Sell, 20, f);
    CHECK(f.size() == 2 && f[0].price == 100 && f[1].price == 98);
    CHECK(b.order_count() == 0);
}

template <class Book>
void cancel_and_unknown_ids() {
    Book b = fresh<Book>();
    std::vector<Fill> f;
    b.add_limit(1, Side::Buy, 100, 5, f);
    CHECK(b.cancel(1));
    CHECK(!b.cancel(1));
    CHECK(!b.cancel(77));
    CHECK(!b.modify(77, 3));
    CHECK(b.order_count() == 0);
}

template <class Book>
void modify_down_keeps_priority_up_loses_it() {
    Book b = fresh<Book>();
    std::vector<Fill> f;
    b.add_limit(1, Side::Sell, 100, 10, f);
    b.add_limit(2, Side::Sell, 100, 10, f);
    b.modify(1, 4);  // smaller: still first
    b.market(9, Side::Buy, 4, f);
    CHECK(f.size() == 1 && f[0].maker == 1);
    f.clear();
    b.add_limit(3, Side::Sell, 100, 10, f);
    b.modify(2, 50);  // bigger: goes behind 3
    b.market(10, Side::Buy, 10, f);
    CHECK(f.size() == 1 && f[0].maker == 3);
}

template <class Book>
void run(const char* name) {
    int before = failures;
    partial_fill_rests_remainder<Book>();
    price_then_time_priority<Book>();
    limit_does_not_trade_through_its_price<Book>();
    market_sweeps_levels_and_drops_rest<Book>();
    cancel_and_unknown_ids<Book>();
    modify_down_keeps_priority_up_loses_it<Book>();
    std::printf("%-14s %s\n", name, failures == before ? "ok" : "FAILED");
}

int main() {
    run<ReferenceBook>("ReferenceBook");
    run<MapBook>("MapBook");
    run<FlatBook>("FlatBook");
    return failures ? 1 : 0;
}
