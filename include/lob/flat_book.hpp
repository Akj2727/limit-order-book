#pragma once
// Optimized book.
//  - Prices are integer ticks inside a fixed band, so each side is a flat array of levels.
//  - Each level is a FIFO doubly linked list threaded through a preallocated node pool.
//  - Order id -> pool slot lives in an open-addressing hash map, so cancel and modify are O(1).
// After construction the hot path does no heap allocation (apart from the caller's fill buffer).
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <vector>

#include "flat_hash_map.hpp"
#include "types.hpp"

namespace lob {

class FlatBook {
public:
    explicit FlatBook(Price max_ticks = 1 << 15, uint32_t max_orders = 1 << 20)
        : max_ticks_(max_ticks),
          pool_(max_orders),
          bids_(max_ticks),
          asks_(max_ticks),
          index_(static_cast<size_t>(max_orders) * 2),
          best_bid_(-1),
          best_ask_(max_ticks) {
        free_.reserve(max_orders);
        for (uint32_t i = max_orders; i-- > 0;) free_.push_back(i);
    }

    void add_limit(OrderId id, Side side, Price price, Qty qty, std::vector<Fill>& out) {
        if (price < 0 || price >= max_ticks_) throw std::out_of_range("price outside band");
        if (side == Side::Buy) {
            qty = match<true>(id, price, qty, out);
            if (qty > 0) rest(id, side, price, qty);
        } else {
            qty = match<false>(id, price, qty, out);
            if (qty > 0) rest(id, side, price, qty);
        }
    }

    void market(OrderId id, Side side, Qty qty, std::vector<Fill>& out) {
        if (side == Side::Buy) match<true>(id, max_ticks_ - 1, qty, out);
        else match<false>(id, 0, qty, out);
    }

    bool cancel(OrderId id) {
        const uint32_t* slot = index_.find(id);
        if (!slot) return false;
        uint32_t n = *slot;
        index_.erase(id);
        remove(n);
        release(n);
        return true;
    }

    bool modify(OrderId id, Qty new_qty) {
        const uint32_t* slot = index_.find(id);
        if (!slot) return false;
        uint32_t n = *slot;
        Node& o = pool_[n];
        if (new_qty <= 0) {
            index_.erase(id);
            remove(n);
            release(n);
        } else if (new_qty <= o.qty) {
            level(o.side, o.price).total -= o.qty - new_qty;  // in place, keeps priority
            o.qty = new_qty;
        } else {
            remove(n);  // a bigger order loses its place and joins the back
            o.qty = new_qty;
            append(n);
        }
        return true;
    }

    Price best_bid() const { return best_bid_; }
    Price best_ask() const { return best_ask_; }
    Qty depth(Side side, Price p) const { return side == Side::Buy ? bids_[p].total : asks_[p].total; }
    size_t order_count() const { return index_.size(); }

    std::vector<Resting> snapshot() const {
        std::vector<Resting> r;
        for (Price p = best_bid_; p >= 0; --p)
            for (uint32_t n = bids_[p].head; n != kNil; n = pool_[n].next)
                r.push_back({Side::Buy, p, pool_[n].id, pool_[n].qty});
        for (Price p = best_ask_; p < max_ticks_; ++p)
            for (uint32_t n = asks_[p].head; n != kNil; n = pool_[n].next)
                r.push_back({Side::Sell, p, pool_[n].id, pool_[n].qty});
        return r;
    }

private:
    static constexpr uint32_t kNil = std::numeric_limits<uint32_t>::max();

    struct Node {
        OrderId id;
        Qty qty;
        Price price;
        Side side;
        uint32_t prev, next;
    };
    struct Level {
        uint32_t head = kNil, tail = kNil;
        Qty total = 0;
    };

    Level& level(Side s, Price p) { return s == Side::Buy ? bids_[p] : asks_[p]; }

    void rest(OrderId id, Side side, Price price, Qty qty) {
        if (free_.empty()) throw std::length_error("order pool exhausted");
        uint32_t n = free_.back();
        free_.pop_back();
        pool_[n] = {id, qty, price, side, kNil, kNil};
        append(n);
        index_.put(id, n);
    }

    void release(uint32_t n) { free_.push_back(n); }

    void append(uint32_t n) {
        Node& o = pool_[n];
        Level& L = level(o.side, o.price);
        o.prev = L.tail;
        o.next = kNil;
        if (L.tail != kNil) pool_[L.tail].next = n;
        else L.head = n;
        L.tail = n;
        L.total += o.qty;
        if (o.side == Side::Buy) best_bid_ = std::max(best_bid_, o.price);
        else best_ask_ = std::min(best_ask_, o.price);
    }

    void remove(uint32_t n) {
        Node& o = pool_[n];
        Level& L = level(o.side, o.price);
        if (o.prev != kNil) pool_[o.prev].next = o.next;
        else L.head = o.next;
        if (o.next != kNil) pool_[o.next].prev = o.prev;
        else L.tail = o.prev;
        L.total -= o.qty;
        if (L.head == kNil) {
            if (o.side == Side::Buy && o.price == best_bid_) advance_bid();
            if (o.side == Side::Sell && o.price == best_ask_) advance_ask();
        }
    }

    void advance_bid() {
        while (best_bid_ >= 0 && bids_[best_bid_].head == kNil) --best_bid_;
    }
    void advance_ask() {
        while (best_ask_ < max_ticks_ && asks_[best_ask_].head == kNil) ++best_ask_;
    }

    // Buy takers walk the asks upward; sell takers walk the bids downward.
    template <bool Buy>
    Qty match(OrderId id, Price limit, Qty qty, std::vector<Fill>& out) {
        while (qty > 0) {
            Price p = Buy ? best_ask_ : best_bid_;
            if (Buy ? (p >= max_ticks_ || p > limit) : (p < 0 || p < limit)) break;
            Level& L = Buy ? asks_[p] : bids_[p];
            while (qty > 0 && L.head != kNil) {
                uint32_t n = L.head;
                Node& m = pool_[n];
                Qty x = std::min(qty, m.qty);
                out.push_back({id, m.id, p, x});
                qty -= x;
                m.qty -= x;
                L.total -= x;
                if (m.qty == 0) {
                    L.head = m.next;
                    if (L.head != kNil) pool_[L.head].prev = kNil;
                    else L.tail = kNil;
                    index_.erase(m.id);
                    release(n);
                }
            }
            if (L.head == kNil) {
                if (Buy) advance_ask();
                else advance_bid();
            }
        }
        return qty;
    }

    Price max_ticks_;
    std::vector<Node> pool_;
    std::vector<uint32_t> free_;
    std::vector<Level> bids_, asks_;
    FlatHashMap index_;
    Price best_bid_, best_ask_;
};

}  // namespace lob
