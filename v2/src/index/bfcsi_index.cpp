#include "caidj/index/bfcsi_index.hpp"

#include <algorithm>
#include <cmath>
#include <mutex>

namespace caidj::index
{
    namespace
    {

        bool is_power_of_two(size_t n)
        {
            return n > 0 && (n & (n - 1U)) == 0;
        }

        size_t next_power_of_two(size_t n)
        {
            size_t p = 1;
            while (p < n)
            {
                p <<= 1U;
            }
            return p;
        }

    } // namespace

    BFCSIIndex::FPCache::FPCache(size_t capacity)
    {
        capacity = std::max<size_t>(1, capacity);
        power_of_two_ = is_power_of_two(capacity);
        if (power_of_two_)
        {
            mask_ = capacity - 1U;
        }
        slots_ = std::vector<std::atomic<Key>>(capacity);
        clear();
    }

    bool BFCSIIndex::FPCache::query(Key key) const
    {
        return slots_[slot_for(key)].load(std::memory_order_relaxed) == key;
    }

    void BFCSIIndex::FPCache::insert(Key key)
    {
        slots_[slot_for(key)].store(key, std::memory_order_relaxed);
    }

    void BFCSIIndex::FPCache::clear()
    {
        for (auto &slot : slots_)
        {
            slot.store(NULL_KEY, std::memory_order_relaxed);
        }
    }

    size_t BFCSIIndex::FPCache::slot_for(Key key) const noexcept
    {
        const auto u = static_cast<uint64_t>(key);
        return power_of_two_ ? static_cast<size_t>(u & mask_) : static_cast<size_t>(u % slots_.size());
    }

    BFCSIIndex::BFCSIIndex(const Config &cfg)
        : fp_cache_(next_power_of_two(cfg.bfcsi_fp_cache_capacity)),
          fpr_(cfg.bfcsi_fpr),
          rebuild_threshold_(cfg.bfcsi_rebuild_threshold)
    {
        num_shards_ = static_cast<size_t>(std::max(1, cfg.bfcsi_num_shards));
        const size_t keys_per_shard = static_cast<size_t>(std::max<int64_t>(1, cfg.r_size / static_cast<int64_t>(num_shards_) + 1));
        bf_shards_.reserve(num_shards_);
        bf_shard_mutexes_.reserve(num_shards_);
        for (size_t i = 0; i < num_shards_; ++i)
        {
            bf_shards_.push_back(std::make_unique<util::BloomFilter>(keys_per_shard, fpr_));
            bf_shard_mutexes_.push_back(std::make_unique<std::shared_mutex>());
        }
    }

    std::vector<TID> BFCSIIndex::probe(Key key)
    {
        probe_count_.fetch_add(1, std::memory_order_relaxed);
        const size_t shard = shard_for(key);
        bool present = false;
        {
            std::shared_lock<std::shared_mutex> lock(*bf_shard_mutexes_[shard]);
            present = bf_shards_[shard]->possibly_present(key);
        }
        if (!present)
        {
            bf_fast_path_.fetch_add(1, std::memory_order_relaxed);
            return {};
        }
        if (fp_cache_.query(key))
        {
            return {};
        }
        auto tids = skiplist_.lookup(key);
        if (tids.empty())
        {
            bf_false_positive_.fetch_add(1, std::memory_order_relaxed);
            fp_cache_.insert(key);
        }
        return tids;
    }

    void BFCSIIndex::insert(Key key, TID tid)
    {
        const size_t shard = shard_for(key);
        {
            std::unique_lock<std::shared_mutex> lock(*bf_shard_mutexes_[shard]);
            bf_shards_[shard]->insert(key);
        }
        skiplist_.insert(key, tid);
        fp_cache_.clear();
        bf_total_keys_.fetch_add(1, std::memory_order_relaxed);
        write_count_.fetch_add(1, std::memory_order_relaxed);
    }

    void BFCSIIndex::remove(Key key, TID tid)
    {
        skiplist_.remove(key, tid);
        write_count_.fetch_add(1, std::memory_order_relaxed);
        if (skiplist_.lookup(key).empty())
        {
            const auto deletes = bf_delete_count_.fetch_add(1, std::memory_order_relaxed) + 1;
            const auto total = std::max<uint64_t>(1, bf_total_keys_.load(std::memory_order_relaxed));
            if (static_cast<double>(deletes) / static_cast<double>(total) >= rebuild_threshold_)
            {
                force_rebuild();
            }
        }
    }

    void BFCSIIndex::bulk_load(const Relation &relation_r)
    {
        skiplist_.clear();
        for (const auto &tuple : relation_r)
        {
            skiplist_.insert(tuple.key, tuple.tid);
            const size_t shard = shard_for(tuple.key);
            std::unique_lock<std::shared_mutex> lock(*bf_shard_mutexes_[shard]);
            bf_shards_[shard]->insert(tuple.key);
        }
        bf_total_keys_.store(static_cast<uint64_t>(relation_r.size()), std::memory_order_release);
        bf_delete_count_.store(0, std::memory_order_release);
        fp_cache_.clear();
    }

    IndexStats BFCSIIndex::get_stats() const
    {
        IndexStats stats;
        stats.probe_count = probe_count_.load(std::memory_order_relaxed);
        stats.write_count = write_count_.load(std::memory_order_relaxed);
        stats.bf_fast_path = bf_fast_path_.load(std::memory_order_relaxed);
        stats.bf_false_positive = bf_false_positive_.load(std::memory_order_relaxed);
        stats.bf_rebuilds = bf_rebuilds_.load(std::memory_order_relaxed);
        uint64_t bf_bytes = 0;
        for (const auto &shard : bf_shards_)
        {
            bf_bytes += shard->memory_bytes();
        }
        stats.memory_bytes = bf_bytes + skiplist_.memory_bytes() + fp_cache_.memory_bytes();
        return stats;
    }

    void BFCSIIndex::force_rebuild()
    {
        bool expected = false;
        if (rebuild_in_progress_.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        {
            rebuild_bloom_filter();
        }
    }

    size_t BFCSIIndex::shard_for(Key key) const noexcept
    {
        const auto u = static_cast<uint64_t>(key);
        return is_power_of_two(num_shards_) ? static_cast<size_t>(u & (num_shards_ - 1U)) : static_cast<size_t>(u % num_shards_);
    }

    void BFCSIIndex::rebuild_bloom_filter()
    {
        std::lock_guard<std::mutex> rebuild_lock(rebuild_mutex_);
        const auto live_keys = skiplist_.all_keys();
        std::vector<std::vector<Key>> by_shard(num_shards_);
        for (Key key : live_keys)
        {
            by_shard[shard_for(key)].push_back(key);
        }
        for (size_t shard = 0; shard < num_shards_; ++shard)
        {
            std::unique_lock<std::shared_mutex> lock(*bf_shard_mutexes_[shard]);
            bf_shards_[shard]->rebuild(by_shard[shard]);
        }
        bf_delete_count_.store(0, std::memory_order_release);
        bf_total_keys_.store(static_cast<uint64_t>(live_keys.size()), std::memory_order_release);
        fp_cache_.clear();
        bf_rebuilds_.fetch_add(1, std::memory_order_relaxed);
        rebuild_in_progress_.store(false, std::memory_order_release);
    }

} // namespace caidj::index