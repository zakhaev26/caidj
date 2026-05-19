#include "caidj/index/nhj_index.hpp"

#include <algorithm>
#include <mutex>

namespace caidj::index
{

    std::vector<TID> NHJIndex::probe(Key key)
    {
        probe_count_.fetch_add(1, std::memory_order_relaxed);
        std::shared_lock<std::shared_mutex> lock(rw_mutex_, std::defer_lock);
        if (!lock.try_lock())
        {
            probe_blocked_.fetch_add(1, std::memory_order_relaxed);
            lock.lock();
        }
        const auto it = table_.find(key);
        if (it == table_.end())
        {
            return {};
        }
        return it->second;
    }

    void NHJIndex::insert(Key key, TID tid)
    {
        std::unique_lock<std::shared_mutex> lock(rw_mutex_);
        table_[key].push_back(tid);
        write_count_.fetch_add(1, std::memory_order_relaxed);
    }

    void NHJIndex::remove(Key key, TID tid)
    {
        std::unique_lock<std::shared_mutex> lock(rw_mutex_);
        auto it = table_.find(key);
        if (it != table_.end())
        {
            auto &tids = it->second;
            tids.erase(std::remove(tids.begin(), tids.end(), tid), tids.end());
            if (tids.empty())
            {
                table_.erase(it);
            }
        }
        write_count_.fetch_add(1, std::memory_order_relaxed);
    }

    void NHJIndex::bulk_load(const Relation &relation_r)
    {
        std::unique_lock<std::shared_mutex> lock(rw_mutex_);
        table_.clear();
        table_.reserve(relation_r.size());
        for (const auto &tuple : relation_r)
        {
            table_[tuple.key].push_back(tuple.tid);
        }
    }

    IndexStats NHJIndex::get_stats() const
    {
        IndexStats stats;
        stats.probe_count = probe_count_.load(std::memory_order_relaxed);
        stats.probe_blocked = probe_blocked_.load(std::memory_order_relaxed);
        stats.write_count = write_count_.load(std::memory_order_relaxed);
        std::shared_lock<std::shared_mutex> lock(rw_mutex_);
        stats.memory_bytes = table_.size() * (sizeof(Key) + sizeof(std::vector<TID>) + 48U);
        for (const auto &[_, tids] : table_)
        {
            stats.memory_bytes += tids.capacity() * sizeof(TID);
        }
        return stats;
    }

} // namespace caidj::index