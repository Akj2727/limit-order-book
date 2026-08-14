#pragma once
// Random order flow for tests and benchmarks: limit orders around a
// mean-reverting mid, some marketable, plus market orders, cancels and modifies.
#include <cstdint>
#include <vector>

#include "types.hpp"

namespace lob {

struct Op {
    enum Type : uint8_t { Limit, Market, Cancel, Modify } type;
    OrderId id;
    Side side;
    Price price;
    Qty qty;
};

class OrderGen {
public:
    OrderGen(uint64_t seed, Price center, Price spread)
        : s_(seed * 0x9E3779B97F4A7C15ULL + 1), center_(center), mid_(center), spread_(spread) {}

    Op next() {
        mid_ += static_cast<Price>(range(-2, 2)) + (center_ - mid_) / 64;
        uint64_t r = u() % 100;
        if (r < 60 || recent_.empty()) return limit();
        if (r < 68) return {Op::Market, next_id_++, side(), 0, range(1, 150)};
        if (r < 93) return {Op::Cancel, pick(), Side::Buy, 0, 0};
        return {Op::Modify, pick(), Side::Buy, 0, range(0, 120)};
    }

private:
    Op limit() {
        Side sd = side();
        // Offsets below zero cross the spread, so some limits trade on arrival.
        Price off = static_cast<Price>(range(-4, spread_));
        Price px = sd == Side::Buy ? mid_ - off : mid_ + off;
        OrderId id = next_id_++;
        remember(id);
        return {Op::Limit, id, sd, px, range(1, 100)};
    }

    void remember(OrderId id) {
        if (recent_.size() < kWindow) recent_.push_back(id);
        else recent_[head_++ % kWindow] = id;
    }
    OrderId pick() { return recent_[u() % recent_.size()]; }
    Side side() { return (u() & 1) ? Side::Buy : Side::Sell; }

    uint64_t u() {
        s_ ^= s_ << 13;
        s_ ^= s_ >> 7;
        s_ ^= s_ << 17;
        return s_;
    }
    int64_t range(int64_t lo, int64_t hi) {
        return lo + static_cast<int64_t>(u() % static_cast<uint64_t>(hi - lo + 1));
    }

    static constexpr size_t kWindow = 4096;
    uint64_t s_;
    Price center_, mid_, spread_;
    OrderId next_id_ = 1;
    std::vector<OrderId> recent_;
    size_t head_ = 0;
};

// Applies one op to any engine with the shared interface.
template <class Book>
bool apply(Book& b, const Op& op, std::vector<Fill>& out) {
    switch (op.type) {
        case Op::Limit: b.add_limit(op.id, op.side, op.price, op.qty, out); return true;
        case Op::Market: b.market(op.id, op.side, op.qty, out); return true;
        case Op::Cancel: return b.cancel(op.id);
        case Op::Modify: return b.modify(op.id, op.qty);
    }
    return false;
}

}  // namespace lob
