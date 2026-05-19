#pragma once

#include "caidj/index/base_index.hpp"

#include <atomic>
#include <shared_mutex>
#include <unordered_map>

namespace caidj::index
{

    class NHJIndex final : public BaseIndex
    {
    public:
        NHJIndex() = default;

        std::vector<TID> probe(Key key) override;
        void insert(Key key, TID tid) override;
        void remove(Key key, TID tid) override;
        void bulk_load(const Relation &relation_r) override;
        IndexStats get_stats() const override;
        Protocol protocol() const noexcept override { return Protocol::NHJ; }

    private:
        std::unordered_map<Key, std::vector<TID>> table_;
        mutable std::shared_mutex rw_mutex_;
        std::atomic<uint64_t> probe_count_{0};
        std::atomic<uint64_t> probe_blocked_{0};
        std::atomic<uint64_t> write_count_{0};
    };

} // namespace caidj::index