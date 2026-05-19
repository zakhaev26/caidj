#include "caidj/txn/txn_manager.hpp"

#include <algorithm>
#include <random>
#include <stdexcept>

namespace caidj::txn
{

    TransactionManager::TransactionManager(index::BaseIndex *index,
                                           DataGen *gen,
                                           int num_write_threads,
                                           uint64_t duration_ms,
                                           double insert_fraction)
        : index_(index),
          gen_(gen),
          num_write_threads_(std::max(0, num_write_threads)),
          duration_ms_(duration_ms),
          insert_fraction_(std::clamp(insert_fraction, 0.0, 1.0))
    {
        if (index_ == nullptr || gen_ == nullptr)
        {
            throw std::invalid_argument("TransactionManager requires non-null index and DataGen pointers");
        }
    }

    TransactionManager::~TransactionManager()
    {
        stop();
    }

    void TransactionManager::start()
    {
        bool expected = false;
        if (!started_.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        {
            return;
        }
        stop_flag_.store(false, std::memory_order_release);
        start_time_ = std::chrono::steady_clock::now();
        stop_time_ = start_time_;
        threads_.reserve(static_cast<size_t>(num_write_threads_));
        for (int i = 0; i < num_write_threads_; ++i)
        {
            threads_.emplace_back(&TransactionManager::writer_thread, this, i);
        }
    }

    void TransactionManager::stop()
    {
        if (!started_.load(std::memory_order_acquire))
        {
            return;
        }
        stop_flag_.store(true, std::memory_order_release);
        for (auto &worker : threads_)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }
        threads_.clear();
        stop_time_ = std::chrono::steady_clock::now();
        started_.store(false, std::memory_order_release);
    }

    TxnStats TransactionManager::get_stats() const
    {
        TxnStats stats;
        stats.total_inserts = total_inserts_.load(std::memory_order_relaxed);
        stats.total_deletes = total_deletes_.load(std::memory_order_relaxed);
        stats.total_writes = stats.total_inserts + stats.total_deletes;
        const auto end = started_.load(std::memory_order_acquire) ? std::chrono::steady_clock::now() : stop_time_;
        const double seconds = std::chrono::duration<double>(end - start_time_).count();
        stats.write_rate_per_sec = seconds <= 0.0 ? 0.0 : static_cast<double>(stats.total_writes) / seconds;
        return stats;
    }

    void TransactionManager::writer_thread(int worker_id)
    {
        std::mt19937_64 rng(static_cast<uint64_t>(worker_id + 1) * 0x9E3779B97F4A7C15ULL);
        std::uniform_real_distribution<double> coin(0.0, 1.0);
        while (!stop_flag_.load(std::memory_order_acquire))
        {
            if (duration_ms_ > 0)
            {
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time_).count();
                if (elapsed >= static_cast<int64_t>(duration_ms_))
                {
                    break;
                }
            }
            const bool do_insert = coin(rng) < insert_fraction_;
            const auto op = do_insert ? OpType::INSERT : OpType::DELETE;
            const auto write = gen_->generate_write_op(op);
            if (do_insert)
            {
                index_->insert(write.key, write.tid);
                total_inserts_.fetch_add(1, std::memory_order_relaxed);
            }
            else
            {
                index_->remove(write.key, write.tid);
                total_deletes_.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }

} // namespace caidj::txn