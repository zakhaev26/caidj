#ifndef TYPES_HPP
#define TYPES_HPP

#include <cstdint>
#include <atomic>
#include <vector>
#include <functional>
#include <memory>
#include <chrono>
#include <optional>

using Key = uint64_t;
using Value = uint64_t;
using Timestamp = uint64_t;

struct [[gnu::packed]] KeyValuePair {
    Key key;
    Value value;
    
    bool operator==(const KeyValuePair& other) const {
        return key == other.key && value == other.value;
    }
};

struct [[gnu::packed]] JoinResult {
    Key key;
    Value left_value;
    Value right_value;
    
    bool operator==(const JoinResult& other) const {
        return key == other.key && 
               left_value == other.left_value && 
               right_value == other.right_value;
    }
};

enum class IndexOpResult {
    SUCCESS,
    NOT_FOUND,
    EXISTS,
    RETRY_NEEDED
};

struct [[gnu::aligned(64)]] CacheLinePaddedAtomic {
    std::atomic<uint64_t> value;
    
    CacheLinePaddedAtomic() : value(0) {}
    explicit CacheLinePaddedAtomic(uint64_t v) : value(v) {}
};

using HighResClock = std::chrono::high_resolution_clock;
using TimePoint = std::chrono::time_point<HighResClock>;
using Duration = std::chrono::nanoseconds;

constexpr size_t CACHE_LINE_SIZE = 64;
constexpr size_t HASH_SEED = 0x9e3779b97f4a7c15ULL;

inline uint64_t hash64(uint64_t x, uint64_t seed = HASH_SEED) {
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    x ^= seed;
    return x;
}

inline uint64_t murmurhash64(uint64_t key, uint64_t seed = 0) {
    const uint64_t m = 0xc6a4a7935bd1e995ULL;
    const int r = 47;
    
    uint64_t h = seed ^ (8 * m);
    
    uint64_t k = key;
    k *= m;
    k ^= k >> r;
    k *= m;
    
    h ^= k;
    h *= m;
    
    h ^= h >> r;
    h *= m;
    h ^= h >> r;
    
    return h;
}

constexpr uint64_t rotate_left(uint64_t x, unsigned s) {
    return (x << s) | (x >> (64 - s));
}

constexpr uint64_t rotate_right(uint64_t x, unsigned s) {
    return (x >> s) | (x << (64 - s));
}

#endif
