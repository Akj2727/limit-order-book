#pragma once
// First version of the book: std::map of price levels, a std::list FIFO per level,
// and an unordered_map from order id to its list iterator for O(1) cancel.
// Correct and simple, but every resting order is a heap allocation.
#include <functional>
#include <limits>
#include <list>
#include <map>
#include <unordered_map>
#include <vector>

#include "types.hpp"

namespace lob {

class MapBook {
public:
    void add_limit(OrderId id, Side side, Price price, Qty qty, std::vector<Fill>& out) {
        if (side == Side::Buy) {
            qty = match(asks_, id, qty, out, [price](Price p) { return p <= price; });
            if (qty > 0) rest(bids_, id, side, price, qty);
        } else {
            qty = match(bids_, id, qty, out, [price](Price p) { return p >= price; });
            if (qty > 0) rest(asks_, id, side, price, qty);
        }
    }

    void market(OrderId id, Side side, Qty qty, std::vector<Fill>& out) {
        auto any = [](Price) { return true; };
        if (side == Side::Buy) match(asks_, id, qty, out, any);
        else match(bids_, id, qty, out, any);
    }

    bool cancel(OrderId id) {
        auto it = index_.find(id);
        if (it == index_.end()) return false;
        erase(it->second);
        index_.erase(it);
        return true;
    }

    bool modify(OrderId id, Qty new_qty) {
        auto it = index_.find(id);
        if (it == index_.end()) return false;
        Loc& loc = it->second;
        if (new_qty <= 0) {
            erase(loc);
            index_.erase(it);
            return true;
        }
        Qty old = loc.it->qty;
        if (new_qty <= old) {
            loc.it->qty = new_qty;  // keeps its place in the queue
        } else {
            Side side = loc.side;
            Price price = loc.price;
            erase(loc);
            index_.erase(it);
            if (side == Side::Buy) rest(bids_, id, side, price, new_qty);
            else rest(asks_, id, side, price, new_qty);
        }
        return true;
    }

    std::vector<Resting> snapshot() const {
        std::vector<Resting> r;
        for (auto& [p, lvl] : bids_)
            for (auto& o : lvl) r.push_back({Side::Buy, p, o.id, o.qty});
        for (auto& [p, lvl] : asks_)
            for (auto& o : lvl) r.push_back({Side::Sell, p, o.id, o.qty});
        return r;
    }

    size_t order_count() const { return index_.size(); }

private:
    struct Ord {
        OrderId id;
        Qty qty;
    };
    using Level = std::list<Ord>;
    struct Loc {
        Side side;
        Price price;
        Level::iterator it;
    };

    template <class Book>
    void rest(Book& book, OrderId id, Side side, Price price, Qty qty) {
        Level& lvl = book[price];
        lvl.push_back({id, qty});
        index_[id] = {side, price, std::prev(lvl.end())};
    }

    void erase(const Loc& loc) {
        if (loc.side == Side::Buy) erase_in(bids_, loc);
        else erase_in(asks_, loc);
    }

    template <class Book>
    void erase_in(Book& book, const Loc& loc) {
        auto lvl = book.find(loc.price);
        lvl->second.erase(loc.it);
        if (lvl->second.empty()) book.erase(lvl);
    }

    template <class Book, class Crosses>
    Qty match(Book& book, OrderId id, Qty qty, std::vector<Fill>& out, Crosses crosses) {
        while (qty > 0 && !book.empty()) {
            auto lvl = book.begin();
            if (!crosses(lvl->first)) break;
            Level& q = lvl->second;
            while (qty > 0 && !q.empty()) {
                Ord& m = q.front();
                Qty x = std::min(qty, m.qty);
                out.push_back({id, m.id, lvl->first, x});
                qty -= x;
                m.qty -= x;
                if (m.qty == 0) {
                    index_.erase(m.id);
                    q.pop_front();
                }
            }
            if (q.empty()) book.erase(lvl);
        }
        return qty;
    }

    std::map<Price, Level, std::greater<Price>> bids_;
    std::map<Price, Level> asks_;
    std::unordered_map<OrderId, Loc> index_;
};

}  // namespace lob
