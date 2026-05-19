#pragma once

#include "caidj/common.hpp"

#include <cstddef>
#include <cstdint>
#include <shared_mutex>
#include <vector>

namespace caidj::util
{

    class BloomFilter
    {
    public:
        BloomFilter(size_t num_keys = 1024, double fpr = 0.01);

        void insert(Key key);
        bool possibly_present(Key key) const;
        void rebuild(const std::vector<Key> &live_keys);

        size_t bit_count() const noexcept { return bit_count_; }
        size_t hash_count() const noexcept { return hash_count_; }
        size_t memory_bytes() const noexcept { return bits_.size(); }

    private:
        uint64_t hash(Key key, uint64_t seed) const noexcept;
        void insert_unlocked(Key key);

        size_t bit_count_ = 8;
        size_t hash_count_ = 1;
        std::vector<uint8_t> bits_;
        mutable std::shared_mutex mutex_;
    };

} // namespace caidj::util