#pragma once
// Reference engine: a flat vector of resting orders with linear scans.
// Slow on purpose. It exists so the fast engines can be checked against it.
#include <algorithm>
#include <limits>
#include <vector>

#include "types.hpp"

namespace lob {

class ReferenceBook {
public:
    void add_limit(OrderId id, Side side, Price price, Qty qty, std::vector<Fill>& out) {
        qty = match(id, side, price, qty, out);
        if (qty > 0) orders_.push_back({side, price, id, qty, seq_++});
    }

    void market(OrderId id, Side side, Qty qty, std::vector<Fill>& out) {
        Price limit = side == Side::Buy ? std::numeric_limits<Price>::max()
                                        : std::numeric_limits<Price>::min();
        match(id, side, limit, qty, out);
    }

    bool cancel(OrderId id) {
        auto it = find(id);
        if (it == orders_.end()) return false;
        orders_.erase(it);
        return true;
    }

    // Reducing size keeps time priority; increasing it sends the order to the back.
    bool modify(OrderId id, Qty new_qty) {
        auto it = find(id);
        if (it == orders_.end()) return false;
        if (new_qty <= 0) {
            orders_.erase(it);
        } else {
            if (new_qty > it->qty) it->seq = seq_++;
            it->qty = new_qty;
        }
        return true;
    }

    std::vector<Resting> snapshot() const {
        std::vector<Order> v = orders_;
        std::sort(v.begin(), v.end(), [](const Order& a, const Order& b) {
            if (a.side != b.side) return a.side < b.side;
            if (a.price != b.price)
                return a.side == Side::Buy ? a.price > b.price : a.price < b.price;
            return a.seq < b.seq;
        });
        std::vector<Resting> r;
        for (auto& o : v) r.push_back({o.side, o.price, o.id, o.qty});
        return r;
    }

    size_t order_count() const { return orders_.size(); }

private:
    struct Order {
        Side side;
        Price price;
        OrderId id;
        Qty qty;
        uint64_t seq;
    };

    std::vector<Order>::iterator find(OrderId id) {
        return std::find_if(orders_.begin(), orders_.end(),
                            [id](const Order& o) { return o.id == id; });
    }

    Qty match(OrderId id, Side side, Price limit, Qty qty, std::vector<Fill>& out) {
        while (qty > 0) {
            auto best = orders_.end();
            for (auto it = orders_.begin(); it != orders_.end(); ++it) {
                if (it->side == side) continue;
                bool crosses = side == Side::Buy ? it->price <= limit : it->price >= limit;
                if (!crosses) continue;
                if (best == orders_.end()) { best = it; continue; }
                bool better = side == Side::Buy ? it->price < best->price : it->price > best->price;
                if (better || (it->price == best->price && it->seq < best->seq)) best = it;
            }
            if (best == orders_.end()) break;
            Qty x = std::min(qty, best->qty);
            out.push_back({id, best->id, best->price, x});
            qty -= x;
            best->qty -= x;
            if (best->qty == 0) orders_.erase(best);
        }
        return qty;
    }

    std::vector<Order> orders_;
    uint64_t seq_ = 0;
};

}  // namespace lob
