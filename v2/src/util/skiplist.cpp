#include "caidj/util/skiplist.hpp"

#include <algorithm>
#include <mutex>

namespace caidj::util
{

    std::vector<TID> ConcurrentSkipList::lookup(Key key) const
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = data_.find(key);
        if (it == data_.end())
        {
            return {};
        }
        return it->second;
    }

    void ConcurrentSkipList::insert(Key key, TID tid)
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto &tids = data_[key];
        if (std::find(tids.begin(), tids.end(), tid) == tids.end())
        {
            tids.push_back(tid);
        }
    }

    void ConcurrentSkipList::remove(Key key, TID tid)
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = data_.find(key);
        if (it == data_.end())
        {
            return;
        }
        auto &tids = it->second;
        tids.erase(std::remove(tids.begin(), tids.end(), tid), tids.end());
        if (tids.empty())
        {
            data_.erase(it);
        }
    }

    std::vector<Key> ConcurrentSkipList::all_keys() const
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        std::vector<Key> keys;
        keys.reserve(data_.size());
        for (const auto &[key, _] : data_)
        {
            keys.push_back(key);
        }
        return keys;
    }

    size_t ConcurrentSkipList::size() const
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        size_t total = 0;
        for (const auto &[_, tids] : data_)
        {
            total += tids.size();
        }
        return total;
    }

    size_t ConcurrentSkipList::memory_bytes() const
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        size_t bytes = data_.size() * (sizeof(Key) + sizeof(std::vector<TID>) + 64U);
        for (const auto &[_, tids] : data_)
        {
            bytes += tids.capacity() * sizeof(TID);
        }
        return bytes;
    }

    void ConcurrentSkipList::clear()
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        data_.clear();
    }

} // namespace caidj::util