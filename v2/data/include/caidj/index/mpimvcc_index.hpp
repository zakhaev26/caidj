#pragma once

#include "caidj/config.hpp"
#include "caidj/index/base_index.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace caidj::index
{

    struct VersionNode
    {
        std::unordered_set<TID> tids;
        TxnID ts_commit{0};
        std::atomic<TxnID> ts_delete{INF_TS};
        std::shared_ptr<VersionNode> next;

        VersionNode() = default;
        VersionNode(std::unordered_set<TID> tids_in, TxnID commit, std::shared_ptr<VersionNode> older = nullptr);
    };

    class MPIMVCCIndex final : public BaseIndex
    {
    public:
        explicit MPIMVCCIndex(const Config &cfg);
        ~MPIMVCCIndex() override;

        CAIDJ_NONCOPYABLE(MPIMVCCIndex);

        std::vector<TID> probe(Key key) override;
        std::vector<TID> probe_at(Key key, TxnID read_ts);
        void insert(Key key, TID tid) override;
        void remove(Key key, TID tid) override;
        void bulk_load(const Relation &relation_r) override;
        IndexStats get_stats() const override;
        Protocol protocol() const noexcept override { return Protocol::MPI_MVCC; }

        TxnID current_timestamp() const noexcept { return global_ts_.load(std::memory_order_acquire); }
        void force_gc_once();

    private:
        std::mutex &mutex_for_key(Key key);
        void apply_write(OpType op, Key key, TID tid);
        void gc_loop();
        void register_read(TxnID ts);
        void unregister_read(TxnID ts);
        TxnID safe_gc_timestamp() const;
        static std::vector<TID> tids_to_sorted_vector(const std::unordered_set<TID> &tids);

        std::unordered_map<Key, std::shared_ptr<VersionNode>> chains_;
        mutable std::shared_mutex chains_rw_;
        std::unordered_map<Key, std::unique_ptr<std::mutex>> key_mutexes_;
        std::mutex key_mutexes_map_mutex_;

        std::atomic<TxnID> global_ts_{0};
        std::atomic<TxnID> min_active_read_ts_{INF_TS};

        std::thread gc_thread_;
        std::atomic<bool> stop_flag_{false};
        uint64_t gc_interval_ms_ = 500;

        mutable std::mutex active_reads_mutex_;
        std::multiset<TxnID> active_read_timestamps_;

        std::atomic<uint64_t> probe_count_{0};
        std::atomic<uint64_t> write_count_{0};
        std::atomic<uint64_t> gc_runs_{0};
        std::atomic<uint64_t> gc_nodes_freed_{0};
    };

} // namespace caidj::index