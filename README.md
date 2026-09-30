# limit-order-book

A price-time priority limit order book and matching engine in C++20.

It supports limit orders, market orders, cancels and modifies, with partial fills. Two engines share one interface:

- **MapBook**, the first version: `std::map` price levels, a `std::list` FIFO per level, and an `unordered_map` from order id to list iterator for O(1) cancel.
- **FlatBook**, the optimized version: integer price ticks index straight into a flat array of levels, each level is an intrusive doubly linked FIFO threaded through a preallocated node pool, and an open-addressing hash map (backward-shift deletion, no tombstones) maps order id to pool slot. After construction the hot path does not allocate.

A slow, linear-scan **ReferenceBook** exists only to check the other two.

## Matching rules

- Best price first, then oldest order first within a price.
- Trades print at the resting order's price.
- A limit order trades as far as its price allows, and any remainder rests.
- A market order trades what it can, and the rest is dropped.
- Reducing an order's size keeps its place in the queue. Increasing it sends the order to the back.

## Correctness

`differential_test` replays random order flow through every engine and the reference. Each op must produce identical fills and cancel/modify results, and the resting book must match at the end of each sequence.

```
$ ./build/differential_test 1000000 100
MapBook: 1000100 sequences, 22500000 ops, 8342793 fills, all match the reference
FlatBook: 1000100 sequences, 22500000 ops, 8342793 fills, all match the reference
```

That covers 1M short sequences (1 to 40 ops, very tight books, lots of crossing) plus 100 long ones of 20,000 ops each. `unit_test` covers the matching rules with hand-written scenarios.

## Performance

5M pre-generated ops (60% limit, 8% market, 25% cancel, 7% modify), best of 5 runs, 2-core Xeon @ 2.1 GHz, `-O2 -march=native`:

| Engine | Throughput | p50 | p99 | p99.9 |
| --- | --- | --- | --- | --- |
| MapBook | 7.58M ops/s | 155 ns | 713 ns | 1483 ns |
| FlatBook | 23.70M ops/s | 120 ns | 400 ns | 568 ns |

Latencies are timed per op with `steady_clock`, so they include a few tens of nanoseconds of timer overhead.

## Build

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cd build && ctest            # unit + differential tests
./bench 5000000 5            # ops, runs
```
