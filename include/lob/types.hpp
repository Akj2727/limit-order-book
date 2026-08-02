#pragma once
#include <cstdint>

namespace lob {

using OrderId = uint64_t;
using Price = int32_t;  // integer ticks
using Qty = int64_t;

enum class Side : uint8_t { Buy = 0, Sell = 1 };

struct Fill {
    OrderId taker;
    OrderId maker;
    Price price;  // always the resting (maker) price
    Qty qty;
    bool operator==(const Fill&) const = default;
};

struct Resting {
    Side side;
    Price price;
    OrderId id;
    Qty qty;
    bool operator==(const Resting&) const = default;
};

}  // namespace lob
