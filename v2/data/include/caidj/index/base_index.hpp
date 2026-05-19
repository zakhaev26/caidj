#pragma once

#include "caidj/common.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace caidj
{
    struct Config;
}

namespace caidj::index
{

    struct IndexStats
    {
        uint64_t probe_count = 0;
        uint64_t probe_blocked = 0;
        uint64_t write_count = 0;
        uint64_t epoch_transitions = 0;
        uint64_t gc_runs = 0;
        uint64_t bf_fast_path = 0;
        uint64_t bf_false_positive = 0;
        uint64_t bf_rebuilds = 0;
        double version_chain_avg = 0.0;
        uint64_t memory_bytes = 0;
    };

    class BaseIndex
    {
    public:
        virtual ~BaseIndex() = default;

        virtual std::vector<TID> probe(Key key) = 0;
        virtual void insert(Key key, TID tid) = 0;
        virtual void remove(Key key, TID tid) = 0;
        virtual void bulk_load(const Relation &relation_r) = 0;
        virtual IndexStats get_stats() const = 0;
        virtual Protocol protocol() const noexcept = 0;
    };

    std::unique_ptr<BaseIndex> make_index(Protocol p, const Config &cfg);

} // namespace caidj::index