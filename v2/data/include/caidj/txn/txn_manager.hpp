#pragma once

#include "caidj/datagen.hpp"
#include "caidj/index/base_index.hpp"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

namespace caidj::txn
{

    struct TxnStats
    {
        uint64_t total_writes = 0;
        uint64_t total_inserts = 0;
        uint64_t total_deletes = 0;
        double write_rate_per_sec = 0.0;
    };

    class TransactionManager
    {
    public:
        TransactionManager(index::BaseIndex *index,
                           DataGen *gen,
                           int num_write_threads,
                           uint64_t duration_ms,
                           double insert_fraction = 0.8);
        ~TransactionManager();

        CAIDJ_NONCOPYABLE(TransactionManager);

        void start();
        void stop();
        TxnStats get_stats() const;

    private:
        void writer_thread(int worker_id);

        index::BaseIndex *index_ = nullptr;
        DataGen *gen_ = nullptr;
        int num_write_threads_ = 0;
        uint64_t duration_ms_ = 0;
        double insert_fraction_ = 0.8;

        std::vector<std::thread> threads_;
        std::atomic<bool> stop_flag_{false};
        std::atomic<bool> started_{false};
        std::chrono::steady_clock::time_point start_time_{};
        std::chrono::steady_clock::time_point stop_time_{};

        std::atomic<uint64_t> total_inserts_{0};
        std::atomic<uint64_t> total_deletes_{0};
    };

} // namespace caidj::txn