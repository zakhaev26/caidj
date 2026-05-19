#pragma once

#include "caidj/config.hpp"
#include "caidj/index/base_index.hpp"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace caidj::index
{

    class ECHIIndex final : public BaseIndex
    {
    public:
        explicit ECHIIndex(const Config &cfg);
        ~ECHIIndex() override;

        CAIDJ_NONCOPYABLE(ECHIIndex);

        std::vector<TID> probe(Key key) override;
        void insert(Key key, TID tid) override;
        void remove(Key key, TID tid) override;
        void bulk_load(const Relation &relation_r) override;
        IndexStats get_stats() const override;
        Protocol protocol() const noexcept override { return Protocol::ECHI; }

        void trigger_epoch_transition();
        uint64_t current_epoch() const noexcept { return epoch_.load(std::memory_order_acquire); }

    private:
        using Map = std::unordered_map<Key, std::vector<TID>>;

        void enqueue_write(WriteOp op);
        void epoch_timer_fn();
        static void apply_delta(Map &map, const std::vector<WriteOp> &delta);
        static uint64_t estimate_memory_bytes(const Map &map, size_t pending_delta);

        std::atomic<std::shared_ptr<const Map>> current_map_;
        mutable std::mutex delta_mutex_;
        std::vector<WriteOp> delta_buffer_;

        std::atomic<uint64_t> epoch_{0};
        std::atomic<int64_t> active_readers_{0};
        std::mutex transition_mutex_;
        std::condition_variable readers_done_cv_;

        size_t delta_threshold_ = 1000;
        uint64_t epoch_interval_ms_ = 100;
        std::thread epoch_timer_thread_;
        std::atomic<bool> stop_flag_{false};

        std::atomic<uint64_t> probe_count_{0};
        std::atomic<uint64_t> probe_blocked_{0};
        std::atomic<uint64_t> write_count_{0};
        std::atomic<uint64_t> epoch_transitions_{0};
    };

} // namespace caidj::index