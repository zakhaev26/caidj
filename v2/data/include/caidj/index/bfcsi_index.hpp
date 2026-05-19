#pragma once

#include "caidj/config.hpp"
#include "caidj/index/base_index.hpp"
#include "caidj/util/bloom_filter.hpp"
#include "caidj/util/skiplist.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <vector>

namespace caidj::index
{

    class BFCSIIndex final : public BaseIndex
    {
    public:
        explicit BFCSIIndex(const Config &cfg);
        ~BFCSIIndex() override = default;

        std::vector<TID> probe(Key key) override;
        void insert(Key key, TID tid) override;
        void remove(Key key, TID tid) override;
        void bulk_load(const Relation &relation_r) override;
        IndexStats get_stats() const override;
        Protocol protocol() const noexcept override { return Protocol::BF_CSI; }

        void force_rebuild();

    private:
        class FPCache
        {
        public:
            explicit FPCache(size_t capacity);
            bool query(Key key) const;
            void insert(Key key);
            size_t memory_bytes() const noexcept { return slots_.size() * sizeof(Key); }
            void clear();

        private:
            size_t slot_for(Key key) const noexcept;
            std::vector<std::atomic<Key>> slots_;
            size_t mask_{};
            bool power_of_two_{true};
        };

        size_t shard_for(Key key) const noexcept;
        void rebuild_bloom_filter();

        size_t num_shards_ = 16;
        std::vector<std::unique_ptr<util::BloomFilter>> bf_shards_;
        std::vector<std::unique_ptr<std::shared_mutex>> bf_shard_mutexes_;
        std::atomic<uint64_t> bf_delete_count_{0};
        std::atomic<uint64_t> bf_total_keys_{0};

        util::ConcurrentSkipList skiplist_;
        FPCache fp_cache_;

        double fpr_ = 0.01;
        double rebuild_threshold_ = 0.20;
        std::mutex rebuild_mutex_;
        std::atomic<bool> rebuild_in_progress_{false};

        std::atomic<uint64_t> probe_count_{0};
        std::atomic<uint64_t> bf_fast_path_{0};
        std::atomic<uint64_t> bf_false_positive_{0};
        std::atomic<uint64_t> bf_rebuilds_{0};
        std::atomic<uint64_t> write_count_{0};
    };

} // namespace caidj::index