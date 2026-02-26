#ifndef SKEW_AWARE_JOIN_HPP
#define SKEW_AWARE_JOIN_HPP

#include <vector>
#include <unordered_map>
#include <atomic>
#include <algorithm>
#include <numeric>
#include <random>
#include <cmath>
#include "types.hpp"
#include "concurrent_index.hpp"

struct MovableAtomic {
    uint64_t value;
    
    MovableAtomic() : value(0) {}
    explicit MovableAtomic(uint64_t v) : value(v) {}
    
    MovableAtomic& operator=(uint64_t v) {
        store(v);
        return *this;
    }
    
    uint64_t load(std::memory_order m = std::memory_order_seq_cst) const {
        return std::atomic_load_explicit(reinterpret_cast<const std::atomic<uint64_t>*>(&value), m);
    }
    
    void store(uint64_t v, std::memory_order m = std::memory_order_seq_cst) {
        std::atomic_store_explicit(reinterpret_cast<std::atomic<uint64_t>*>(&value), v, m);
    }
    
    uint64_t fetch_add(uint64_t v, std::memory_order m = std::memory_order_seq_cst) {
        return std::atomic_fetch_add_explicit(reinterpret_cast<std::atomic<uint64_t>*>(&value), v, m);
    }
};

struct HotKeyInfo {
    Key key;
    uint64_t frequency;
    uint64_t partition_id;
    bool is_hot;
    
    HotKeyInfo() : key(0), frequency(0), partition_id(0), is_hot(false) {}
    HotKeyInfo(Key k, uint64_t f, uint64_t p, bool h) 
        : key(k), frequency(f), partition_id(p), is_hot(h) {}
};

struct alignas(CACHE_LINE_SIZE) PartitionStats {
    MovableAtomic tuple_count;
    MovableAtomic hot_key_count;
    MovableAtomic contention_events;
    uint64_t padding[8];
    
    PartitionStats() {
        tuple_count = MovableAtomic(0);
        hot_key_count = MovableAtomic(0);
        contention_events = MovableAtomic(0);
    }
    
    void reset() {
        tuple_count.store(0, std::memory_order_relaxed);
        hot_key_count.store(0, std::memory_order_relaxed);
        contention_events.store(0, std::memory_order_relaxed);
    }
};

class SkewAwareJoinExecutor {
public:
    static constexpr double DEFAULT_HOT_KEY_RATIO = 0.01;
    static constexpr uint64_t SAMPLE_SIZE = 10000;
    static constexpr uint64_t MIN_HOT_KEY_FREQUENCY = 100;
    static constexpr uint32_t NUM_PARTITIONS = 64;
    
    struct SkewConfig {
        uint32_t num_threads;
        uint64_t dataset_size;
        double zipf_theta;
        bool enable_adaptive;
        uint32_t hot_key_threshold;
        
        SkewConfig() 
            : num_threads(4), 
              dataset_size(1000000), 
              zipf_theta(0.5),
              enable_adaptive(true),
              hot_key_threshold(1000) {}
    };
    
private:
    SkewConfig config_;
    std::vector<PartitionStats> partition_stats_;
    std::unordered_map<Key, HotKeyInfo> hot_keys_;
    std::atomic<uint64_t> hot_key_count_{0};
    std::atomic<bool> sampling_complete_{false};
    std::atomic<uint64_t> adaptive_threshold_{1000};
    
    std::vector<std::vector<KeyValuePair>> build_partitions_;
    std::vector<std::vector<KeyValuePair>> probe_partitions_;
    
    std::vector<MovableAtomic> thread_work_counts_;
    std::vector<MovableAtomic> thread_contention_counts_;
    
public:
    explicit SkewAwareJoinExecutor(const SkewConfig& config) : config_(config) {
        partition_stats_.resize(NUM_PARTITIONS);
        build_partitions_.resize(NUM_PARTITIONS);
        probe_partitions_.resize(NUM_PARTITIONS);
        thread_work_counts_.resize(config_.num_threads);
        thread_contention_counts_.resize(config_.num_threads);
    }
    
    void generateDataset(std::vector<KeyValuePair>& build_relation,
                        std::vector<KeyValuePair>& probe_relation,
                        double skew_factor = 0.0) {
        build_relation.resize(config_.dataset_size);
        probe_relation.resize(config_.dataset_size * 2);
        
        std::mt19937_64 rng(42);
        
        if (skew_factor > 0.0) {
            generateSkewedData(build_relation, probe_relation, rng, skew_factor);
        } else {
            generateUniformData(build_relation, probe_relation, rng);
        }
        
        for (size_t i = 0; i < config_.num_threads; ++i) {
            thread_work_counts_[i] = 0;
            thread_contention_counts_[i] = 0;
        }
    }
    
    void sampleAndDetectHotKeys(const std::vector<KeyValuePair>& data) {
        if (sampling_complete_.load(std::memory_order_acquire)) {
            return;
        }
        
        std::unordered_map<Key, uint64_t> sample_counts;
        size_t sample_step = (data.size() / SAMPLE_SIZE);
        if (sample_step < 1) sample_step = 1;
        
        for (size_t i = 0; i < data.size(); i += sample_step) {
            Key key = data[i].key;
            sample_counts[key]++;
        }
        
        uint64_t total_sample = sample_counts.size();
        uint64_t hot_count = 0;
        
        uint64_t threshold = (MIN_HOT_KEY_FREQUENCY > static_cast<uint64_t>(SAMPLE_SIZE * DEFAULT_HOT_KEY_RATIO)) 
            ? MIN_HOT_KEY_FREQUENCY 
            : static_cast<uint64_t>(SAMPLE_SIZE * DEFAULT_HOT_KEY_RATIO);
        
        for (const auto& [key, count] : sample_counts) {
            if (count >= threshold) {
                uint32_t partition = hash64(key) % NUM_PARTITIONS;
                hot_keys_[key] = HotKeyInfo(key, count * (data.size() / SAMPLE_SIZE), 
                                             partition, true);
                hot_count++;
                
                partition_stats_[partition].hot_key_count.fetch_add(1, 
                    std::memory_order_relaxed);
            }
        }
        
        hot_key_count_.store(hot_count, std::memory_order_release);
        sampling_complete_.store(true, std::memory_order_release);
    }
    
    uint32_t getPartition(Key key) {
        auto it = hot_keys_.find(key);
        if (it != hot_keys_.end() && it->second.is_hot) {
            return it->second.partition_id;
        }
        return hash64(key) % NUM_PARTITIONS;
    }
    
    void partitionRelations(const std::vector<KeyValuePair>& build,
                           const std::vector<KeyValuePair>& probe) {
        for (const auto& kv : build) {
            uint32_t partition = getPartition(kv.key);
            build_partitions_[partition].push_back(kv);
            partition_stats_[partition].tuple_count.fetch_add(1, 
                std::memory_order_relaxed);
        }
        
        for (const auto& kv : probe) {
            uint32_t partition = getPartition(kv.key);
            probe_partitions_[partition].push_back(kv);
        }
    }
    
    std::vector<JoinResult> executeJoinParallel(ARTIndex& index, 
                                                  uint32_t num_threads) {
        std::vector<JoinResult> results;
        results.reserve(config_.dataset_size);
        
        std::vector<std::thread> workers;
        std::atomic<uint64_t> completed_partitions{0};
        
        auto worker_func = [&](uint32_t thread_id) {
            uint64_t local_results = 0;
            uint64_t local_contention = 0;
            
            for (uint32_t p = thread_id; p < NUM_PARTITIONS; p += num_threads) {
                if (partition_stats_[p].hot_key_count.load(std::memory_order_relaxed) > 0) {
                    local_contention++;
                    partition_stats_[p].contention_events.fetch_add(1,
                        std::memory_order_relaxed);
                }
                
                for (const auto& probe_kv : probe_partitions_[p]) {
                    auto value = index.find(probe_kv.key);
                    if (value) {
                        for (const auto& build_kv : build_partitions_[p]) {
                            if (build_kv.key == probe_kv.key) {
                                results.push_back({probe_kv.key, build_kv.value, 
                                                   *value});
                                local_results++;
                            }
                        }
                    }
                }
            }
            
            thread_work_counts_[thread_id].fetch_add(local_results, 
                std::memory_order_relaxed);
            thread_contention_counts_[thread_id].fetch_add(local_contention,
                std::memory_order_relaxed);
        };
        
        for (uint32_t i = 0; i < num_threads; ++i) {
            workers.emplace_back(worker_func, i);
        }
        
        for (auto& w : workers) {
            w.join();
        }
        
        return results;
    }
    
    std::vector<JoinResult> executeSkewAwareJoin(ARTIndex& index,
                                                  uint32_t num_threads) {
        sampleAndDetectHotKeys(build_partitions_.empty() ? probe_partitions_[0] : 
                              build_partitions_[0]);
        
        std::vector<std::thread> workers;
        std::vector<std::vector<JoinResult>> thread_results(num_threads);
        std::atomic<uint64_t> hot_key_processed{0};
        
        auto skew_aware_worker = [&](uint32_t thread_id) {
            uint64_t processed = 0;
            
            for (uint32_t p = thread_id; p < NUM_PARTITIONS; p += num_threads) {
                const auto& build_partition = build_partitions_[p];
                const auto& probe_partition = probe_partitions_[p];
                
                if (partition_stats_[p].hot_key_count.load(std::memory_order_relaxed) > 0) {
                    auto& results = thread_results[thread_id];
                    
                    std::unordered_map<Key, std::vector<Value>> hot_results;
                    
                    for (const auto& probe_kv : probe_partition) {
                        auto it = hot_keys_.find(probe_kv.key);
                        if (it != hot_keys_.end() && it->second.is_hot) {
                            for (const auto& build_kv : build_partition) {
                                if (build_kv.key == probe_kv.key) {
                                    results.push_back({probe_kv.key, build_kv.value,
                                                       probe_kv.value});
                                    processed++;
                                }
                            }
                            hot_key_processed.fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                    
                    for (const auto& probe_kv : probe_partition) {
                        auto it = hot_keys_.find(probe_kv.key);
                        if (it == hot_keys_.end() || !it->second.is_hot) {
                            auto value = index.find(probe_kv.key);
                            if (value) {
                                for (const auto& build_kv : build_partition) {
                                    if (build_kv.key == probe_kv.key) {
                                        results.push_back({probe_kv.key, build_kv.value,
                                                           *value});
                                        processed++;
                                    }
                                }
                            }
                        }
                    }
                } else {
                    auto& results = thread_results[thread_id];
                    for (const auto& probe_kv : probe_partition) {
                        auto value = index.find(probe_kv.key);
                        if (value) {
                            for (const auto& build_kv : build_partition) {
                                if (build_kv.key == probe_kv.key) {
                                    results.push_back({probe_kv.key, build_kv.value,
                                                       *value});
                                    processed++;
                                }
                            }
                        }
                    }
                }
            }
            
            thread_work_counts_[thread_id].fetch_add(processed, std::memory_order_relaxed);
        };
        
        for (uint32_t i = 0; i < num_threads; ++i) {
            workers.emplace_back(skew_aware_worker, i);
        }
        
        for (auto& w : workers) {
            w.join();
        }
        
        std::vector<JoinResult> results;
        for (auto& tr : thread_results) {
            results.insert(results.end(), tr.begin(), tr.end());
        }
        
        return results;
    }
    
    void buildIndex(ARTIndex& index, const std::vector<KeyValuePair>& build_relation) {
        for (const auto& kv : build_relation) {
            index.insert(kv.key, kv.value);
        }
    }
    
    void reset() {
        for (auto& p : partition_stats_) {
            p.reset();
        }
        
        hot_keys_.clear();
        build_partitions_.clear();
        build_partitions_.resize(NUM_PARTITIONS);
        probe_partitions_.clear();
        probe_partitions_.resize(NUM_PARTITIONS);
        
        hot_key_count_.store(0, std::memory_order_release);
        sampling_complete_.store(false, std::memory_order_release);
        
        for (auto& c : thread_work_counts_) {
            c.store(0, std::memory_order_relaxed);
        }
        for (auto& c : thread_contention_counts_) {
            c.store(0, std::memory_order_relaxed);
        }
    }
    
    uint64_t getHotKeyCount() const {
        return hot_key_count_.load(std::memory_order_relaxed);
    }
    
    uint64_t getTotalContentionEvents() const {
        uint64_t total = 0;
        for (const auto& p : partition_stats_) {
            total += p.contention_events.load(std::memory_order_relaxed);
        }
        return total;
    }
    
    uint64_t getThreadWorkCount(uint32_t thread_id) const {
        return thread_work_counts_[thread_id].load(std::memory_order_relaxed);
    }
    
    void setAdaptiveThreshold(uint64_t threshold) {
        adaptive_threshold_.store(threshold, std::memory_order_release);
    }
    
private:
    void generateUniformData(std::vector<KeyValuePair>& build,
                             std::vector<KeyValuePair>& probe,
                             std::mt19937_64& rng) {
        std::uniform_int_distribution<Key> key_dist(0, config_.dataset_size - 1);
        std::uniform_int_distribution<Value> value_dist(0, UINT64_MAX);
        
        for (size_t i = 0; i < build.size(); ++i) {
            build[i] = {key_dist(rng), value_dist(rng)};
        }
        
        for (size_t i = 0; i < probe.size(); ++i) {
            probe[i] = {key_dist(rng), value_dist(rng)};
        }
    }
    
    void generateSkewedData(std::vector<KeyValuePair>& build,
                           std::vector<KeyValuePair>& probe,
                           std::mt19937_64& rng,
                           double theta) {
        double zeta = 0.0;
        for (uint64_t i = 1; i <= config_.dataset_size; ++i) {
            zeta += 1.0 / std::pow(i, theta);
        }
        
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        
        auto zipf_sample = [&]() -> Key {
            double u = dist(rng);
            double sum = 0.0;
            for (uint64_t i = 1; i <= config_.dataset_size; ++i) {
                sum += 1.0 / std::pow(i, theta);
                if (u <= sum / zeta) {
                    return i - 1;
                }
            }
            return config_.dataset_size - 1;
        };
        
        std::uniform_int_distribution<Value> value_dist(0, UINT64_MAX);
        
        for (size_t i = 0; i < build.size(); ++i) {
            build[i] = {zipf_sample(), value_dist(rng)};
        }
        
        for (size_t i = 0; i < probe.size(); ++i) {
            probe[i] = {zipf_sample(), value_dist(rng)};
        }
    }
};

#endif
