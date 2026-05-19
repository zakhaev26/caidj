#include "caidj/index/echi_index.hpp"

#include <algorithm>
#include <chrono>
#include <mutex>

namespace caidj::index
{

    ECHIIndex::ECHIIndex(const Config &cfg)
        : current_map_(std::make_shared<Map>()),
          delta_threshold_(cfg.echi_delta_threshold),
          epoch_interval_ms_(cfg.echi_epoch_interval_ms)
    {
        if (epoch_interval_ms_ > 0)
        {
            epoch_timer_thread_ = std::thread(&ECHIIndex::epoch_timer_fn, this);
        }
    }

    ECHIIndex::~ECHIIndex()
    {
        stop_flag_.store(true, std::memory_order_release);
        if (epoch_timer_thread_.joinable())
        {
            epoch_timer_thread_.join();
        }
        trigger_epoch_transition();
    }

    std::vector<TID> ECHIIndex::probe(Key key)
    {
        probe_count_.fetch_add(1, std::memory_order_relaxed);
        active_readers_.fetch_add(1, std::memory_order_acquire);
        auto guard = [this]()
        {
            if (active_readers_.fetch_sub(1, std::memory_order_release) == 1)
            {
                readers_done_cv_.notify_all();
            }
        };

        auto snap = current_map_.load(std::memory_order_acquire);
        std::vector<TID> result;
        if (snap)
        {
            const auto it = snap->find(key);
            if (it != snap->end())
            {
                result = it->second;
            }
        }
        guard();
        return result;
    }

    void ECHIIndex::insert(Key key, TID tid)
    {
        enqueue_write(WriteOp{OpType::INSERT, key, tid});
    }

    void ECHIIndex::remove(Key key, TID tid)
    {
        enqueue_write(WriteOp{OpType::DELETE, key, tid});
    }

    void ECHIIndex::bulk_load(const Relation &relation_r)
    {
        auto map = std::make_shared<Map>();
        map->reserve(relation_r.size());
        for (const auto &tuple : relation_r)
        {
            (*map)[tuple.key].push_back(tuple.tid);
        }
        current_map_.store(map, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(delta_mutex_);
            delta_buffer_.clear();
        }
    }

    IndexStats ECHIIndex::get_stats() const
    {
        IndexStats stats;
        stats.probe_count = probe_count_.load(std::memory_order_relaxed);
        stats.probe_blocked = probe_blocked_.load(std::memory_order_relaxed);
        stats.write_count = write_count_.load(std::memory_order_relaxed);
        stats.epoch_transitions = epoch_transitions_.load(std::memory_order_relaxed);
        size_t pending = 0;
        {
            std::lock_guard<std::mutex> lock(delta_mutex_);
            pending = delta_buffer_.size();
        }
        const auto snap = current_map_.load(std::memory_order_acquire);
        stats.memory_bytes = snap ? estimate_memory_bytes(*snap, pending) : 0;
        return stats;
    }

    void ECHIIndex::enqueue_write(WriteOp op)
    {
        bool should_transition = false;
        {
            std::lock_guard<std::mutex> lock(delta_mutex_);
            delta_buffer_.push_back(op);
            should_transition = delta_buffer_.size() >= delta_threshold_;
        }
        write_count_.fetch_add(1, std::memory_order_relaxed);
        if (should_transition)
        {
            trigger_epoch_transition();
        }
    }

    void ECHIIndex::trigger_epoch_transition()
    {
        std::unique_lock<std::mutex> transition_lock(transition_mutex_);
        readers_done_cv_.wait(transition_lock, [this]()
                              { return active_readers_.load(std::memory_order_acquire) == 0; });

        std::vector<WriteOp> local_delta;
        {
            std::lock_guard<std::mutex> lock(delta_mutex_);
            local_delta.swap(delta_buffer_);
        }

        auto current = current_map_.load(std::memory_order_acquire);
        auto next = std::make_shared<Map>(current ? *current : Map{});
        apply_delta(*next, local_delta);
        current_map_.store(next, std::memory_order_release);
        epoch_.fetch_add(1, std::memory_order_seq_cst);
        epoch_transitions_.fetch_add(1, std::memory_order_relaxed);
    }

    void ECHIIndex::epoch_timer_fn()
    {
        while (!stop_flag_.load(std::memory_order_acquire))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(epoch_interval_ms_));
            if (!stop_flag_.load(std::memory_order_acquire))
            {
                trigger_epoch_transition();
            }
        }
    }

    void ECHIIndex::apply_delta(Map &map, const std::vector<WriteOp> &delta)
    {
        for (const auto &op : delta)
        {
            if (op.op == OpType::INSERT)
            {
                map[op.key].push_back(op.tid);
            }
            else
            {
                auto it = map.find(op.key);
                if (it == map.end())
                {
                    continue;
                }
                auto &tids = it->second;
                tids.erase(std::remove(tids.begin(), tids.end(), op.tid), tids.end());
                if (tids.empty())
                {
                    map.erase(it);
                }
            }
        }
    }

    uint64_t ECHIIndex::estimate_memory_bytes(const Map &map, size_t pending_delta)
    {
        uint64_t bytes = static_cast<uint64_t>(map.size() * (sizeof(Key) + sizeof(std::vector<TID>) + 48U));
        for (const auto &[_, tids] : map)
        {
            bytes += static_cast<uint64_t>(tids.capacity() * sizeof(TID));
        }
        bytes += static_cast<uint64_t>(pending_delta * sizeof(WriteOp));
        return bytes;
    }

} // namespace caidj::index