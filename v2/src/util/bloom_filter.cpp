#include "caidj/util/bloom_filter.hpp"

#include <algorithm>
#include <cmath>
#include <mutex>

namespace caidj::util
{
    namespace
    {

        uint64_t splitmix64(uint64_t x) noexcept
        {
            x += 0x9E3779B97F4A7C15ULL;
            x = (x ^ (x >> 30U)) * 0xBF58476D1CE4E5B9ULL;
            x = (x ^ (x >> 27U)) * 0x94D049BB133111EBULL;
            return x ^ (x >> 31U);
        }

    } // namespace

    BloomFilter::BloomFilter(size_t num_keys, double fpr)
    {
        num_keys = std::max<size_t>(1, num_keys);
        fpr = std::clamp(fpr, 1e-9, 0.999999);
        const double ln2 = std::log(2.0);
        bit_count_ = static_cast<size_t>(std::ceil(-static_cast<double>(num_keys) * std::log(fpr) / (ln2 * ln2)));
        bit_count_ = std::max<size_t>(8, bit_count_);
        hash_count_ = static_cast<size_t>(std::round((static_cast<double>(bit_count_) / static_cast<double>(num_keys)) * ln2));
        hash_count_ = std::max<size_t>(1, hash_count_);
        bits_.assign((bit_count_ + 7U) / 8U, 0);
    }

    uint64_t BloomFilter::hash(Key key, uint64_t seed) const noexcept
    {
        return splitmix64(static_cast<uint64_t>(key) ^ seed);
    }

    void BloomFilter::insert_unlocked(Key key)
    {
        for (size_t i = 0; i < hash_count_; ++i)
        {
            const uint64_t seed = 0xDEADBEEFULL + i * 0x9E3779B9ULL;
            const auto idx = static_cast<size_t>(hash(key, seed) % bit_count_);
            bits_[idx >> 3U] = static_cast<uint8_t>(bits_[idx >> 3U] | (uint8_t{1} << (idx & 7U)));
        }
    }

    void BloomFilter::insert(Key key)
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        insert_unlocked(key);
    }

    bool BloomFilter::possibly_present(Key key) const
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        for (size_t i = 0; i < hash_count_; ++i)
        {
            const uint64_t seed = 0xDEADBEEFULL + i * 0x9E3779B9ULL;
            const auto idx = static_cast<size_t>(hash(key, seed) % bit_count_);
            if ((bits_[idx >> 3U] & (uint8_t{1} << (idx & 7U))) == 0)
            {
                return false;
            }
        }
        return true;
    }

    void BloomFilter::rebuild(const std::vector<Key> &live_keys)
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        std::fill(bits_.begin(), bits_.end(), uint8_t{0});
        for (Key key : live_keys)
        {
            insert_unlocked(key);
        }
    }

} // namespace caidj::util