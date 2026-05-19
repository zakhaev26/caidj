#include "caidj/index/mpimvcc_index.hpp"

#include <algorithm>
#include <chrono>
#include <mutex>

namespace caidj::index
{

    VersionNode::VersionNode(std::unordered_set<TID> tids_in, TxnID commit, std::shared_ptr<VersionNode> older)
        : tids(std::move(tids_in)), ts_commit(commit), ts_delete(INF_TS), next(std::move(older)) {}

    MPIMVCCIndex::MPIMVCCIndex(const Config &cfg) : gc_interval_ms_(cfg.mpimvcc_gc_interval_ms)
    {
        if (gc_interval_ms_ > 0)
        {
            gc_thread_ = std::thread(&MPIMVCCIndex::gc_loop, this);
        }
    }

    MPIMVCCIndex::~MPIMVCCIndex()
    {
        stop_flag_.store(true, std::memory_order_release);
        if (gc_thread_.joinable())
        {
            gc_thread_.join();
        }
    }

    std::vector<TID> MPIMVCCIndex::probe(Key key)
    {
        const TxnID read_ts = global_ts_.load(std::memory_order_acquire);
        return probe_at(key, read_ts);
    }

    std::vector<TID> MPIMVCCIndex::probe_at(Key key, TxnID read_ts)
    {
        probe_count_.fetch_add(1, std::memory_order_relaxed);
        register_read(read_ts);
        std::vector<TID> result;
        {
            std::shared_lock<std::shared_mutex> lock(chains_rw_);
            auto it = chains_.find(key);
            if (it != chains_.end())
            {
                auto node = it->second;
                while (node)
                {
                    const TxnID delete_ts = node->ts_delete.load(std::memory_order_acquire);
                    if (node->ts_commit <= read_ts && read_ts < delete_ts)
                    {
                        result = tids_to_sorted_vector(node->tids);
                        break;
                    }
                    node = node->next;
                }
            }
        }
        unregister_read(read_ts);
        return result;
    }

    void MPIMVCCIndex::insert(Key key, TID tid)
    {
        apply_write(OpType::INSERT, key, tid);
    }

    void MPIMVCCIndex::remove(Key key, TID tid)
    {
        apply_write(OpType::DELETE, key, tid);
    }

    void MPIMVCCIndex::bulk_load(const Relation &relation_r)
    {
        std::unordered_map<Key, std::unordered_set<TID>> grouped;
        grouped.reserve(relation_r.size());
        for (const auto &tuple : relation_r)
        {
            grouped[tuple.key].insert(tuple.tid);
        }
        {
            std::unique_lock<std::shared_mutex> lock(chains_rw_);
            chains_.clear();
            chains_.reserve(grouped.size());
            for (auto &[key, tids] : grouped)
            {
                chains_[key] = std::make_shared<VersionNode>(std::move(tids), 0, nullptr);
            }
        }
        global_ts_.store(0, std::memory_order_release);
    }

    IndexStats MPIMVCCIndex::get_stats() const
    {
        IndexStats stats;
        stats.probe_count = probe_count_.load(std::memory_order_relaxed);
        stats.write_count = write_count_.load(std::memory_order_relaxed);
        stats.gc_runs = gc_runs_.load(std::memory_order_relaxed);

        std::shared_lock<std::shared_mutex> lock(chains_rw_);
        uint64_t total_chain_nodes = 0;
        uint64_t key_count = 0;
        uint64_t tid_count = 0;
        for (const auto &[_, head] : chains_)
        {
            ++key_count;
            auto node = head;
            while (node)
            {
                ++total_chain_nodes;
                tid_count += node->tids.size();
                node = node->next;
            }
        }
        stats.version_chain_avg = key_count == 0 ? 0.0 : static_cast<double>(total_chain_nodes) / static_cast<double>(key_count);
        stats.memory_bytes = total_chain_nodes * (sizeof(VersionNode) + 64U) + tid_count * sizeof(TID);
        return stats;
    }

    std::mutex &MPIMVCCIndex::mutex_for_key(Key key)
    {
        std::lock_guard<std::mutex> lock(key_mutexes_map_mutex_);
        auto &ptr = key_mutexes_[key];
        if (!ptr)
        {
            ptr = std::make_unique<std::mutex>();
        }
        return *ptr;
    }

    void MPIMVCCIndex::apply_write(OpType op, Key key, TID tid)
    {
        const TxnID write_ts = global_ts_.fetch_add(1, std::memory_order_seq_cst) + 1;
        std::mutex &key_mutex = mutex_for_key(key);
        std::lock_guard<std::mutex> key_lock(key_mutex);

        std::shared_ptr<VersionNode> head;
        {
            std::shared_lock<std::shared_mutex> lock(chains_rw_);
            auto it = chains_.find(key);
            if (it != chains_.end())
            {
                head = it->second;
            }
        }

        std::unordered_set<TID> new_tids = head ? head->tids : std::unordered_set<TID>{};
        if (op == OpType::INSERT)
        {
            new_tids.insert(tid);
        }
        else
        {
            new_tids.erase(tid);
        }

        auto new_node = std::make_shared<VersionNode>(std::move(new_tids), write_ts, head);
        if (head)
        {
            head->ts_delete.store(write_ts, std::memory_order_release);
        }
        {
            std::unique_lock<std::shared_mutex> lock(chains_rw_);
            chains_[key] = std::move(new_node);
        }
        write_count_.fetch_add(1, std::memory_order_relaxed);
    }

    void MPIMVCCIndex::gc_loop()
    {
        while (!stop_flag_.load(std::memory_order_acquire))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(gc_interval_ms_));
            if (!stop_flag_.load(std::memory_order_acquire))
            {
                force_gc_once();
            }
        }
    }

    void MPIMVCCIndex::force_gc_once()
    {
        const TxnID safe_ts = safe_gc_timestamp();
        std::unique_lock<std::shared_mutex> lock(chains_rw_);
        for (auto &[_, head] : chains_)
        {
            auto node = head;
            while (node && node->next)
            {
                auto next = node->next;
                if (next->ts_delete.load(std::memory_order_acquire) < safe_ts)
                {
                    node->next = next->next;
                    gc_nodes_freed_.fetch_add(1, std::memory_order_relaxed);
                }
                else
                {
                    node = node->next;
                }
            }
        }
        gc_runs_.fetch_add(1, std::memory_order_relaxed);
    }

    void MPIMVCCIndex::register_read(TxnID ts)
    {
        std::lock_guard<std::mutex> lock(active_reads_mutex_);
        active_read_timestamps_.insert(ts);
        min_active_read_ts_.store(*active_read_timestamps_.begin(), std::memory_order_release);
    }

    void MPIMVCCIndex::unregister_read(TxnID ts)
    {
        std::lock_guard<std::mutex> lock(active_reads_mutex_);
        auto it = active_read_timestamps_.find(ts);
        if (it != active_read_timestamps_.end())
        {
            active_read_timestamps_.erase(it);
        }
        min_active_read_ts_.store(active_read_timestamps_.empty() ? INF_TS : *active_read_timestamps_.begin(), std::memory_order_release);
    }

    TxnID MPIMVCCIndex::safe_gc_timestamp() const
    {
        return min_active_read_ts_.load(std::memory_order_acquire);
    }

    std::vector<TID> MPIMVCCIndex::tids_to_sorted_vector(const std::unordered_set<TID> &tids)
    {
        std::vector<TID> out(tids.begin(), tids.end());
        std::sort(out.begin(), out.end());
        return out;
    }

} // namespace caidj::index