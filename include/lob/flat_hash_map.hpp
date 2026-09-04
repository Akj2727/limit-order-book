#pragma once
// Open-addressing hash map from order id to pool slot.
// Linear probing with backward-shift deletion, so there are no tombstones and
// probe chains stay short under heavy add/cancel churn. Never allocates after construction.
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace lob {

class FlatHashMap {
public:
    // capacity is rounded up to a power of two; keep load under ~50%.
    explicit FlatHashMap(size_t capacity) {
        size_t cap = 16;
        while (cap < capacity) cap <<= 1;
        keys_.assign(cap, kEmpty);
        vals_.assign(cap, 0);
        mask_ = cap - 1;
    }

    void put(uint64_t key, uint32_t val) {
        size_t i = slot(key);
        while (keys_[i] != kEmpty && keys_[i] != key) i = (i + 1) & mask_;
        if (keys_[i] == kEmpty && ++size_ > (mask_ + 1) * 3 / 4)
            throw std::length_error("FlatHashMap over capacity");
        keys_[i] = key;
        vals_[i] = val;
    }

    const uint32_t* find(uint64_t key) const {
        size_t i = slot(key);
        while (keys_[i] != kEmpty) {
            if (keys_[i] == key) return &vals_[i];
            i = (i + 1) & mask_;
        }
        return nullptr;
    }

    bool erase(uint64_t key) {
        size_t i = slot(key);
        while (keys_[i] != key) {
            if (keys_[i] == kEmpty) return false;
            i = (i + 1) & mask_;
        }
        // Shift later entries of the cluster back so lookups never hit a hole.
        size_t j = i;
        for (;;) {
            j = (j + 1) & mask_;
            if (keys_[j] == kEmpty) break;
            size_t home = slot(keys_[j]);
            bool stays = i <= j ? (i < home && home <= j) : (i < home || home <= j);
            if (stays) continue;
            keys_[i] = keys_[j];
            vals_[i] = vals_[j];
            i = j;
        }
        keys_[i] = kEmpty;
        --size_;
        return true;
    }

    size_t size() const { return size_; }

private:
    static constexpr uint64_t kEmpty = 0;  // order ids start at 1

    size_t slot(uint64_t k) const {
        k ^= k >> 33;
        k *= 0xff51afd7ed558ccdULL;
        k ^= k >> 33;
        return k & mask_;
    }

    std::vector<uint64_t> keys_;
    std::vector<uint32_t> vals_;
    size_t mask_ = 0;
    size_t size_ = 0;
};

}  // namespace lob
