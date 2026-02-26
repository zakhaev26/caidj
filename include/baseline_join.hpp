#ifndef BASELINE_JOIN_HPP
#define BASELINE_JOIN_HPP

#include <vector>
#include <thread>
#include <atomic>
#include <algorithm>
#include <random>
#include "types.hpp"
#include "baseline_index.hpp"

class BaselineNaiveJoin {
public:
    struct Config {
        uint32_t num_threads;
        uint64_t dataset_size;
        
        Config() : num_threads(4), dataset_size(1000000) {}
    };
    
private:
    Config config_;
    
public:
    explicit BaselineNaiveJoin(const Config& config) : config_(config) {}
    
    void generateDataset(std::vector<KeyValuePair>& build,
                        std::vector<KeyValuePair>& probe) {
        build.resize(config_.dataset_size);
        probe.resize(config_.dataset_size * 2);
        
        std::mt19937_64 rng(42);
        std::uniform_int_distribution<Key> key_dist(0, config_.dataset_size - 1);
        std::uniform_int_distribution<Value> value_dist(0, UINT64_MAX);
        
        for (size_t i = 0; i < build.size(); ++i) {
            build[i] = {key_dist(rng), value_dist(rng)};
        }
        
        for (size_t i = 0; i < probe.size(); ++i) {
            probe[i] = {key_dist(rng), value_dist(rng)};
        }
    }
    
    void buildIndex(BaselineLockedIndex& index, 
                   const std::vector<KeyValuePair>& build) {
        for (const auto& kv : build) {
            index.insert(kv.key, kv.value);
        }
    }
    
    std::vector<JoinResult> executeJoin(BaselineLockedIndex& index,
                                       const std::vector<KeyValuePair>& probe) {
        std::vector<JoinResult> results;
        results.reserve(probe.size() / 10);
        
        for (const auto& probe_kv : probe) {
            auto value = index.find(probe_kv.key);
            if (value) {
                results.push_back({probe_kv.key, *value, probe_kv.value});
            }
        }
        
        return results;
    }
    
    std::vector<JoinResult> executeJoinParallel(BaselineLockedIndex& index,
                                                 const std::vector<KeyValuePair>& probe,
                                                 uint32_t num_threads) {
        std::vector<std::vector<JoinResult>> thread_results(num_threads);
        std::vector<std::thread> workers;
        
        size_t chunk_size = probe.size() / num_threads;
        
        auto worker_func = [&](uint32_t thread_id) {
            size_t start = thread_id * chunk_size;
            size_t end = (thread_id == num_threads - 1) ? probe.size() : start + chunk_size;
            
            for (size_t i = start; i < end; ++i) {
                auto value = index.find(probe[i].key);
                if (value) {
                    thread_results[thread_id].push_back({probe[i].key, *value, probe[i].value});
                }
            }
        };
        
        for (uint32_t i = 0; i < num_threads; ++i) {
            workers.emplace_back(worker_func, i);
        }
        
        for (auto& w : workers) {
            w.join();
        }
        
        std::vector<JoinResult> results;
        for (const auto& tr : thread_results) {
            results.insert(results.end(), tr.begin(), tr.end());
        }
        
        return results;
    }
    
    std::vector<JoinResult> executeHashJoin(const std::vector<KeyValuePair>& build,
                                            const std::vector<KeyValuePair>& probe) {
        std::unordered_map<Key, std::vector<Value>> hash_table;
        
        for (const auto& kv : build) {
            hash_table[kv.key].push_back(kv.value);
        }
        
        std::vector<JoinResult> results;
        
        for (const auto& probe_kv : probe) {
            auto it = hash_table.find(probe_kv.key);
            if (it != hash_table.end()) {
                for (const auto& build_value : it->second) {
                    results.push_back({probe_kv.key, build_value, probe_kv.value});
                }
            }
        }
        
        return results;
    }
};

#endif
