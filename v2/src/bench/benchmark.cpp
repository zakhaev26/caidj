#include "caidj/bench/benchmark.hpp"

#include "caidj/datagen.hpp"
#include "caidj/index/base_index.hpp"
#include "caidj/util/logger.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace caidj::bench
{
    namespace
    {

        std::string json_escape(const std::string &s)
        {
            std::ostringstream oss;
            for (char c : s)
            {
                switch (c)
                {
                case '"':
                    oss << "\\\"";
                    break;
                case '\\':
                    oss << "\\\\";
                    break;
                case '\n':
                    oss << "\\n";
                    break;
                default:
                    oss << c;
                    break;
                }
            }
            return oss.str();
        }

        std::string index_stats_json(const index::IndexStats &stats, int indent)
        {
            const std::string pad(static_cast<size_t>(indent), ' ');
            std::ostringstream oss;
            oss << "{\n";
            oss << pad << "  \"probe_count\": " << stats.probe_count << ",\n";
            oss << pad << "  \"probe_blocked\": " << stats.probe_blocked << ",\n";
            oss << pad << "  \"write_count\": " << stats.write_count << ",\n";
            oss << pad << "  \"epoch_transitions\": " << stats.epoch_transitions << ",\n";
            oss << pad << "  \"gc_runs\": " << stats.gc_runs << ",\n";
            oss << pad << "  \"bf_fast_path\": " << stats.bf_fast_path << ",\n";
            oss << pad << "  \"bf_false_positive\": " << stats.bf_false_positive << ",\n";
            oss << pad << "  \"bf_rebuilds\": " << stats.bf_rebuilds << ",\n";
            oss << pad << "  \"version_chain_avg\": " << stats.version_chain_avg << ",\n";
            oss << pad << "  \"memory_bytes\": " << stats.memory_bytes << "\n";
            oss << pad << "}";
            return oss.str();
        }

        std::string txn_stats_json(const txn::TxnStats &stats, int indent)
        {
            const std::string pad(static_cast<size_t>(indent), ' ');
            std::ostringstream oss;
            oss << "{\n";
            oss << pad << "  \"total_writes\": " << stats.total_writes << ",\n";
            oss << pad << "  \"total_inserts\": " << stats.total_inserts << ",\n";
            oss << pad << "  \"total_deletes\": " << stats.total_deletes << ",\n";
            oss << pad << "  \"write_rate_per_sec\": " << stats.write_rate_per_sec << "\n";
            oss << pad << "}";
            return oss.str();
        }

        std::string result_json(const BenchmarkResult &r, int indent)
        {
            const std::string pad(static_cast<size_t>(indent), ' ');
            std::ostringstream oss;
            oss << "{\n";
            oss << pad << "  \"protocol\": \"" << json_escape(protocol_to_string(r.protocol)) << "\",\n";
            oss << pad << "  \"concurrency\": " << r.concurrency << ",\n";
            oss << pad << "  \"run_id\": " << r.run_id << ",\n";
            oss << pad << "  \"join_latency_ms\": " << r.join_latency_ms << ",\n";
            oss << pad << "  \"throughput_jps\": " << r.throughput_jps << ",\n";
            oss << pad << "  \"lock_contention_rate\": " << r.lock_contention_rate << ",\n";
            oss << pad << "  \"memory_overhead_mb\": " << r.memory_overhead_mb << ",\n";
            oss << pad << "  \"build_time_ms\": " << r.build_time_ms << ",\n";
            oss << pad << "  \"index_stats\": " << index_stats_json(r.index_stats, indent + 2) << ",\n";
            oss << pad << "  \"txn_stats\": " << txn_stats_json(r.txn_stats, indent + 2) << "\n";
            oss << pad << "}";
            return oss.str();
        }

    } // namespace

    Benchmark::Benchmark(Config cfg) : cfg_(std::move(cfg))
    {
        cfg_.validate();
    }

    BenchmarkResult Benchmark::run_single(Protocol p, int concurrency, int run_id)
    {
        auto *logger = util::get_logger("bench");
        LOG_INFO(logger, "Starting run", protocol_to_string(p), "concurrency", concurrency, "run", run_id);

        DataGen gen(cfg_.seed + static_cast<uint64_t>(run_id), cfg_.zipf_alpha, cfg_.domain_size);
        const Relation r = gen.generate_R(cfg_.r_size);
        const Relation s = gen.generate_S(cfg_.s_size);
        auto idx = index::make_index(p, cfg_);

        const auto build_start = std::chrono::high_resolution_clock::now();
        idx->bulk_load(r);
        const auto build_stop = std::chrono::high_resolution_clock::now();
        const double build_ms = std::chrono::duration<double, std::milli>(build_stop - build_start).count();

        txn::TransactionManager txn_mgr(idx.get(), &gen, concurrency, cfg_.trial_duration_ms, cfg_.insert_fraction);
        join::JoinExecutor executor(idx.get(), cfg_.num_probe_threads, s);

        txn_mgr.start();
        auto join_stats = executor.execute();
        txn_mgr.stop();

        BenchmarkResult result;
        result.protocol = p;
        result.concurrency = concurrency;
        result.run_id = run_id;
        result.join_latency_ms = join_stats.wall_time_ms;
        result.throughput_jps = join_stats.throughput_jps;
        result.index_stats = idx->get_stats();
        result.join_stats = join_stats;
        result.txn_stats = txn_mgr.get_stats();
        result.build_time_ms = build_ms;
        result.lock_contention_rate = result.index_stats.probe_count == 0
                                          ? 0.0
                                          : static_cast<double>(result.index_stats.probe_blocked) / static_cast<double>(result.index_stats.probe_count);
        result.memory_overhead_mb = static_cast<double>(result.index_stats.memory_bytes) / (1024.0 * 1024.0);

        LOG_INFO(logger, "Trial complete latency_ms", result.join_latency_ms, "throughput_jps", result.throughput_jps);
        return result;
    }

    std::vector<BenchmarkResult> Benchmark::run_all()
    {
        std::vector<BenchmarkResult> results;
        for (Protocol p : cfg_.protocols)
        {
            for (int concurrency : cfg_.concurrency_levels)
            {
                for (int run = 1; run <= cfg_.num_runs; ++run)
                {
                    results.push_back(run_single(p, concurrency, run));
                }
            }
        }
        return results;
    }

    void Benchmark::save_results(const std::vector<BenchmarkResult> &results, const std::string &output_path) const
    {
        std::filesystem::create_directories(output_path);
        const std::filesystem::path dir(output_path);

        if (cfg_.write_json)
        {
            std::ofstream out(dir / "results.json");
            if (!out)
            {
                throw std::runtime_error("could not write results.json");
            }
            out << "{\n";
            out << "  \"version\": \"1.0.0\",\n";
            out << "  \"config\": " << config_to_json(cfg_) << ",\n";
            out << "  \"results\": [\n";
            for (size_t i = 0; i < results.size(); ++i)
            {
                out << "    " << result_json(results[i], 4);
                if (i + 1 < results.size())
                {
                    out << ',';
                }
                out << '\n';
            }
            out << "  ]\n";
            out << "}\n";
        }

        if (cfg_.write_csv)
        {
            std::ofstream out(dir / "results.csv");
            if (!out)
            {
                throw std::runtime_error("could not write results.csv");
            }
            out << "protocol,concurrency,run_id,join_latency_ms,throughput_jps,lock_contention_rate,memory_overhead_mb,build_time_ms,probe_count,probe_blocked,write_count,gc_runs,bf_fast_path,bf_false_positive,bf_rebuilds\n";
            for (const auto &r : results)
            {
                out << protocol_to_string(r.protocol) << ','
                    << r.concurrency << ','
                    << r.run_id << ','
                    << r.join_latency_ms << ','
                    << r.throughput_jps << ','
                    << r.lock_contention_rate << ','
                    << r.memory_overhead_mb << ','
                    << r.build_time_ms << ','
                    << r.index_stats.probe_count << ','
                    << r.index_stats.probe_blocked << ','
                    << r.index_stats.write_count << ','
                    << r.index_stats.gc_runs << ','
                    << r.index_stats.bf_fast_path << ','
                    << r.index_stats.bf_false_positive << ','
                    << r.index_stats.bf_rebuilds << '\n';
            }
        }
    }

    void print_summary_table(const std::vector<BenchmarkResult> &results)
    {
        std::cout << "\nCAIDJ Benchmark Results\n";
        std::cout << std::left << std::setw(12) << "Protocol" << std::right << std::setw(12) << "Concurrency"
                  << std::setw(16) << "Latency(ms)" << std::setw(16) << "Throughput" << std::setw(16) << "Contention" << '\n';
        std::cout << std::string(72, '-') << '\n';
        for (const auto &r : results)
        {
            std::cout << std::left << std::setw(12) << protocol_to_string(r.protocol)
                      << std::right << std::setw(12) << r.concurrency
                      << std::setw(16) << std::fixed << std::setprecision(2) << r.join_latency_ms
                      << std::setw(16) << std::fixed << std::setprecision(2) << r.throughput_jps
                      << std::setw(15) << std::fixed << std::setprecision(2) << (100.0 * r.lock_contention_rate) << "%\n";
        }
    }

} // namespace caidj::bench