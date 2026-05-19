#pragma once

#include "caidj/config.hpp"
#include "caidj/index/base_index.hpp"
#include "caidj/join/join_executor.hpp"
#include "caidj/txn/txn_manager.hpp"

#include <string>
#include <vector>

namespace caidj::bench
{

    struct BenchmarkResult
    {
        Protocol protocol = Protocol::NHJ;
        int concurrency = 1;
        int run_id = 1;
        double join_latency_ms = 0.0;
        double throughput_jps = 0.0;
        double lock_contention_rate = 0.0;
        double memory_overhead_mb = 0.0;
        double build_time_ms = 0.0;
        index::IndexStats index_stats;
        txn::TxnStats txn_stats;
        join::JoinStats join_stats;
    };

    class Benchmark
    {
    public:
        explicit Benchmark(Config cfg);

        BenchmarkResult run_single(Protocol p, int concurrency, int run_id = 1);
        std::vector<BenchmarkResult> run_all();
        void save_results(const std::vector<BenchmarkResult> &results, const std::string &output_path) const;

    private:
        Config cfg_;
    };

    void print_summary_table(const std::vector<BenchmarkResult> &results);

} // namespace caidj::bench