#include "caidj/join/join_executor.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <vector>

namespace caidj::join
{

    JoinExecutor::JoinExecutor(index::BaseIndex *index, int num_probe_threads, const Relation &relation_s)
        : index_(index), num_probe_threads_(std::max(1, num_probe_threads)), relation_s_(relation_s)
    {
        if (index_ == nullptr)
        {
            throw std::invalid_argument("JoinExecutor requires a non-null index");
        }
    }

    JoinStats JoinExecutor::execute()
    {
        struct LocalStats
        {
            uint64_t probes = 0;
            uint64_t matches = 0;
            uint64_t result_pairs = 0;
        };

        const auto start = std::chrono::high_resolution_clock::now();
        const size_t n = relation_s_.size();
        const int threads = static_cast<int>(std::min<size_t>(static_cast<size_t>(num_probe_threads_), std::max<size_t>(1, n)));
        std::vector<LocalStats> local(static_cast<size_t>(threads));
        std::vector<std::thread> workers;
        workers.reserve(static_cast<size_t>(threads));

        for (int worker = 0; worker < threads; ++worker)
        {
            const size_t begin = n * static_cast<size_t>(worker) / static_cast<size_t>(threads);
            const size_t end = n * static_cast<size_t>(worker + 1) / static_cast<size_t>(threads);
            workers.emplace_back([this, begin, end, &local, worker]()
                                 {
            auto& stats = local[static_cast<size_t>(worker)];
            for (size_t i = begin; i < end; ++i) {
                const auto tids = index_->probe(relation_s_[i].key);
                ++stats.probes;
                if (!tids.empty()) {
                    ++stats.matches;
                    stats.result_pairs += tids.size();
                }
            } });
        }

        for (auto &worker : workers)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }

        const auto stop = std::chrono::high_resolution_clock::now();
        const auto elapsed = std::chrono::duration<double, std::milli>(stop - start).count();

        JoinStats out;
        for (const auto &stats : local)
        {
            out.total_probes += stats.probes;
            out.total_matches += stats.matches;
            out.total_result_pairs += stats.result_pairs;
        }
        out.wall_time_ms = elapsed;
        out.throughput_jps = elapsed <= 0.0 ? 0.0 : static_cast<double>(out.total_probes) / (elapsed / 1000.0);
        out.index_stats = index_->get_stats();
        return out;
    }

} // namespace caidj::join