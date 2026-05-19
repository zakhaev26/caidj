#pragma once

#include "caidj/common.hpp"
#include "caidj/index/base_index.hpp"

#include <cstdint>

namespace caidj::join
{

    struct JoinStats
    {
        uint64_t total_probes = 0;
        uint64_t total_matches = 0;
        uint64_t total_result_pairs = 0;
        double wall_time_ms = 0.0;
        double throughput_jps = 0.0;
        index::IndexStats index_stats;
    };

    class JoinExecutor
    {
    public:
        JoinExecutor(index::BaseIndex *index, int num_probe_threads, const Relation &relation_s);
        JoinStats execute();

    private:
        index::BaseIndex *index_ = nullptr;
        int num_probe_threads_ = 1;
        const Relation &relation_s_;
    };

} // namespace caidj::join