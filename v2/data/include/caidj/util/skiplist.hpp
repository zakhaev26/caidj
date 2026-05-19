#pragma once

#include "caidj/common.hpp"

#include <map>
#include <shared_mutex>
#include <vector>

namespace caidj::util {

class ConcurrentSkipList {
public:
    std::vector<TID> lookup(Key key) const;
    void insert(Key key, TID tid);
    void remove(Key key, TID tid);
    std::vector<Key> all_keys() const;
    size_t size() const;
    size_t memory_bytes() const;
    void clear();

private:
    mutable std::shared_mutex mutex_;
    std::map<Key, std::vector<TID>> data_;
};

} // namespace caidj::util