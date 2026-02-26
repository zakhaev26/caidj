#ifndef BENCHMARK_HPP
#define BENCHMARK_HPP

#include <vector>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <numeric>
#include <algorithm>
#include <atomic>
#include <random>
#include <cmath>
#include <sstream>
#include "types.hpp"
#include "concurrent_index.hpp"
#include "skew_aware_join.hpp"
#include "baseline_index.hpp"
#include "baseline_join.hpp"
#include "bw_tree.hpp"

struct BenchmarkResult {
    double throughput_ops;
    double avg_latency_ns;
    double p50_latency_ns;
    double p95_latency_ns;
    double p99_latency_ns;
    double max_latency_ns;
    uint64_t total_operations;
    uint64_t contention_events;
    double scalability_score;
    
    BenchmarkResult() 
        : throughput_ops(0), avg_latency_ns(0), p50_latency_ns(0),
          p95_latency_ns(0), p99_latency_ns(0), max_latency_ns(0),
          total_operations(0), contention_events(0), scalability_score(0) {}
};

class BenchmarkRunner {
public:
    struct BenchmarkConfig {
        uint32_t num_threads;
        uint64_t dataset_size;
        double skew_factor;
        bool run_baseline;
        bool run_concurrent;
        bool run_skew_aware;
        uint32_t warmup_iterations;
        uint32_t measurement_iterations;
        
        BenchmarkConfig() 
            : num_threads(4),
              dataset_size(1000000),
              skew_factor(0.0),
              run_baseline(true),
              run_concurrent(true),
              run_skew_aware(true),
              warmup_iterations(2),
              measurement_iterations(5) {}
    };
    
private:
    BenchmarkConfig config_;
    std::vector<BenchmarkResult> results_;
    
public:
    explicit BenchmarkRunner(const BenchmarkConfig& config) : config_(config) {}
    
    void runAllBenchmarks() {
        std::cout << "=== Starting Benchmark Suite ===" << std::endl;
        std::cout << "Configuration:" << std::endl;
        std::cout << "  Threads: " << config_.num_threads << std::endl;
        std::cout << "  Dataset Size: " << config_.dataset_size << std::endl;
        std::cout << "  Skew Factor: " << config_.skew_factor << std::endl;
        std::cout << std::endl;
        
        if (config_.run_baseline) {
            runBaselineLockedIndexBenchmark();
        }
        
        if (config_.run_concurrent) {
            // runBwTreeBenchmark(); // Disabled - needs fix
            runConcurrentARTIndexBenchmark();
        }
        
        if (config_.run_skew_aware) {
            runSkewAwareJoinBenchmark();
        }
        
        printResults();
    }
    
    void runBaselineLockedIndexBenchmark() {
        std::cout << "Running Baseline Locked Index Benchmark..." << std::endl;
        
        std::vector<KeyValuePair> data;
        data.resize(config_.dataset_size);
        
        std::mt19937_64 rng(42);
        std::uniform_int_distribution<Key> key_dist(0, config_.dataset_size - 1);
        std::uniform_int_distribution<Value> value_dist(0, UINT64_MAX);
        
        for (auto& kv : data) {
            kv = {key_dist(rng), value_dist(rng)};
        }
        
        for (uint32_t w = 0; w < config_.warmup_iterations; ++w) {
            BaselineLockedIndex index;
            for (const auto& kv : data) {
                index.insert(kv.key, kv.value);
            }
        }
        
        std::vector<double> latencies;
        std::atomic<uint64_t> total_ops{0};
        
        for (uint32_t iter = 0; iter < config_.measurement_iterations; ++iter) {
            BaselineLockedIndex index;
            
            for (const auto& kv : data) {
                index.insert(kv.key, kv.value);
            }
            
            auto start = HighResClock::now();
            
            for (const auto& kv : data) {
                index.find(kv.key);
            }
            
            auto end = HighResClock::now();
            
            auto duration = std::chrono::duration_cast<Duration>(end - start).count();
            latencies.push_back(static_cast<double>(duration));
            total_ops.fetch_add(data.size(), std::memory_order_relaxed);
        }
        
        BenchmarkResult result;
        calculateLatencyStats(latencies, result);
        result.total_operations = total_ops.load() / config_.measurement_iterations;
        
        double total_time = std::accumulate(latencies.begin(), latencies.end(), 0.0);
        result.throughput_ops = (result.total_operations * 1e9) / total_time;
        
        results_.push_back(result);
        
        std::cout << "  Throughput: " << std::fixed << std::setprecision(2) 
                  << result.throughput_ops << " ops/sec" << std::endl;
        std::cout << "  Avg Latency: " << result.avg_latency_ns << " ns" << std::endl;
    }
    
    void runConcurrentARTIndexBenchmark() {
        std::cout << "Running Concurrent ART Index Benchmark..." << std::endl;
        
        std::vector<KeyValuePair> data;
        data.resize(config_.dataset_size);
        
        std::mt19937_64 rng(42);
        std::uniform_int_distribution<Key> key_dist(0, config_.dataset_size - 1);
        std::uniform_int_distribution<Value> value_dist(0, UINT64_MAX);
        
        for (auto& kv : data) {
            kv = {key_dist(rng), value_dist(rng)};
        }
        
        for (uint32_t w = 0; w < config_.warmup_iterations; ++w) {
            ARTIndex index;
            for (const auto& kv : data) {
                index.insert(kv.key, kv.value);
            }
        }
        
        std::vector<double> latencies;
        std::atomic<uint64_t> total_ops{0};
        
        for (uint32_t iter = 0; iter < config_.measurement_iterations; ++iter) {
            ARTIndex index;
            
            for (const auto& kv : data) {
                index.insert(kv.key, kv.value);
            }
            
            auto start = HighResClock::now();
            
            for (const auto& kv : data) {
                index.find(kv.key);
            }
            
            auto end = HighResClock::now();
            
            auto duration = std::chrono::duration_cast<Duration>(end - start).count();
            latencies.push_back(static_cast<double>(duration));
            total_ops.fetch_add(data.size(), std::memory_order_relaxed);
        }
        
        BenchmarkResult result;
        calculateLatencyStats(latencies, result);
        result.total_operations = total_ops.load() / config_.measurement_iterations;
        
        double total_time = std::accumulate(latencies.begin(), latencies.end(), 0.0);
        result.throughput_ops = (result.total_operations * 1e9) / total_time;
        
        results_.push_back(result);
        
        std::cout << "  Throughput: " << std::fixed << std::setprecision(2) 
                  << result.throughput_ops << " ops/sec" << std::endl;
        std::cout << "  Avg Latency: " << result.avg_latency_ns << " ns" << std::endl;
    }
    
    void runBwTreeBenchmark() {
        std::cout << "Running BW-Tree Index Benchmark..." << std::endl;
        
        std::vector<KeyValuePair> data;
        data.resize(config_.dataset_size);
        
        std::mt19937_64 rng(42);
        std::uniform_int_distribution<Key> key_dist(0, config_.dataset_size - 1);
        std::uniform_int_distribution<Value> value_dist(0, UINT64_MAX);
        
        for (auto& kv : data) {
            kv = {key_dist(rng), value_dist(rng)};
        }
        
        for (uint32_t w = 0; w < config_.warmup_iterations; ++w) {
            BwTree index;
            for (const auto& kv : data) {
                index.insert(kv.key, kv.value);
            }
        }
        
        std::vector<double> latencies;
        std::atomic<uint64_t> total_ops{0};
        
        for (uint32_t iter = 0; iter < config_.measurement_iterations; ++iter) {
            BwTree index;
            
            for (const auto& kv : data) {
                index.insert(kv.key, kv.value);
            }
            
            auto start = HighResClock::now();
            
            for (const auto& kv : data) {
                index.find(kv.key);
            }
            
            auto end = HighResClock::now();
            
            auto duration = std::chrono::duration_cast<Duration>(end - start).count();
            latencies.push_back(static_cast<double>(duration));
            total_ops.fetch_add(data.size(), std::memory_order_relaxed);
        }
        
        BenchmarkResult result;
        calculateLatencyStats(latencies, result);
        result.total_operations = total_ops.load() / config_.measurement_iterations;
        
        double total_time = std::accumulate(latencies.begin(), latencies.end(), 0.0);
        result.throughput_ops = (result.total_operations * 1e9) / total_time;
        
        results_.push_back(result);
        
        std::cout << "  Throughput: " << std::fixed << std::setprecision(2) 
                  << result.throughput_ops << " ops/sec" << std::endl;
        std::cout << "  Avg Latency: " << result.avg_latency_ns << " ns" << std::endl;
    }
    
    void runSkewAwareJoinBenchmark() {
        std::cout << "Running Skew-Aware Join Benchmark..." << std::endl;
        
        SkewAwareJoinExecutor::SkewConfig join_config;
        join_config.num_threads = config_.num_threads;
        join_config.dataset_size = config_.dataset_size;
        join_config.zipf_theta = config_.skew_factor;
        
        SkewAwareJoinExecutor executor(join_config);
        
        std::vector<KeyValuePair> build_relation;
        std::vector<KeyValuePair> probe_relation;
        
        executor.generateDataset(build_relation, probe_relation, config_.skew_factor);
        
        std::vector<double> latencies;
        std::atomic<uint64_t> total_ops{0};
        
        for (uint32_t w = 0; w < config_.warmup_iterations; ++w) {
            executor.reset();
            ARTIndex index;
            executor.buildIndex(index, build_relation);
        }
        
        for (uint32_t iter = 0; iter < config_.measurement_iterations; ++iter) {
            executor.reset();
            
            ARTIndex index;
            executor.buildIndex(index, build_relation);
            executor.partitionRelations(build_relation, probe_relation);
            
            auto start = HighResClock::now();
            
            auto results = executor.executeSkewAwareJoin(index, config_.num_threads);
            
            auto end = HighResClock::now();
            
            auto duration = std::chrono::duration_cast<Duration>(end - start).count();
            latencies.push_back(static_cast<double>(duration));
            total_ops.fetch_add(results.size(), std::memory_order_relaxed);
        }
        
        BenchmarkResult result;
        calculateLatencyStats(latencies, result);
        result.total_operations = total_ops.load() / config_.measurement_iterations;
        
        double total_time = std::accumulate(latencies.begin(), latencies.end(), 0.0);
        if (total_time > 0) {
            result.throughput_ops = (result.total_operations * 1e9) / total_time;
        }
        
        result.contention_events = executor.getTotalContentionEvents();
        
        results_.push_back(result);
        
        std::cout << "  Throughput: " << std::fixed << std::setprecision(2) 
                  << result.throughput_ops << " ops/sec" << std::endl;
        std::cout << "  Avg Latency: " << result.avg_latency_ns << " ns" << std::endl;
        std::cout << "  Contention Events: " << result.contention_events << std::endl;
    }
    
    void runScalabilityTest() {
        std::cout << "=== Running Scalability Test ===" << std::endl;
        
        std::vector<uint32_t> thread_counts = {1, 2, 4, 8, 16};
        std::vector<double> throughputs;
        
        for (uint32_t threads : thread_counts) {
            config_.num_threads = threads;
            
            std::vector<KeyValuePair> data;
            data.resize(config_.dataset_size);
            
            std::mt19937_64 rng(42);
            std::uniform_int_distribution<Key> key_dist(0, config_.dataset_size - 1);
            std::uniform_int_distribution<Value> value_dist(0, UINT64_MAX);
            
            for (auto& kv : data) {
                kv = {key_dist(rng), value_dist(rng)};
            }
            
            std::vector<std::thread> workers;
            std::atomic<uint64_t> completed{0};
            
            ARTIndex index;
            
            auto start = HighResClock::now();
            
            auto worker_func = [&](uint32_t tid) {
                size_t chunk = data.size() / threads;
                size_t start_idx = tid * chunk;
                size_t end_idx = (tid == threads - 1) ? data.size() : start_idx + chunk;
                
                for (size_t i = start_idx; i < end_idx; ++i) {
                    index.insert(data[i].key, data[i].value);
                }
                
                for (size_t i = start_idx; i < end_idx; ++i) {
                    index.find(data[i].key);
                }
                
                completed.fetch_add(chunk * 2, std::memory_order_relaxed);
            };
            
            for (uint32_t i = 0; i < threads; ++i) {
                workers.emplace_back(worker_func, i);
            }
            
            for (auto& w : workers) {
                w.join();
            }
            
            auto end = HighResClock::now();
            auto duration = std::chrono::duration_cast<Duration>(end - start).count();
            
            double throughput = (completed.load() * 1e9) / duration;
            throughputs.push_back(throughput);
            
            std::cout << "  Threads: " << threads << " -> Throughput: " 
                      << throughput << " ops/sec" << std::endl;
        }
        
        if (throughputs.size() >= 2) {
            double speedup = throughputs.back() / throughputs.front();
            std::cout << "\n  Speedup (16/1 threads): " << speedup << "x" << std::endl;
        }
    }
    
private:
    void calculateLatencyStats(const std::vector<double>& latencies, BenchmarkResult& result) {
        if (latencies.empty()) return;
        
        std::vector<double> sorted = latencies;
        std::sort(sorted.begin(), sorted.end());
        
        result.avg_latency_ns = std::accumulate(sorted.begin(), sorted.end(), 0.0) / sorted.size();
        
        size_t n = sorted.size();
        result.p50_latency_ns = sorted[n * 50 / 100];
        result.p95_latency_ns = sorted[n * 95 / 100];
        result.p99_latency_ns = sorted[n * 99 / 100];
        result.max_latency_ns = sorted.back();
    }
    
    void printResults() {
        std::cout << "\n=== Benchmark Results Summary ===" << std::endl;
        
        const char* names[] = {"Baseline Locked", "Concurrent ART", "Skew-Aware Join"};
        
        for (size_t i = 0; i < results_.size() && i < 3; ++i) {
            std::cout << "\n" << names[i] << ":" << std::endl;
            std::cout << "  Throughput: " << std::fixed << std::setprecision(2) 
                      << results_[i].throughput_ops << " ops/sec" << std::endl;
            std::cout << "  Avg Latency: " << results_[i].avg_latency_ns << " ns" << std::endl;
            std::cout << "  P50 Latency: " << results_[i].p50_latency_ns << " ns" << std::endl;
            std::cout << "  P95 Latency: " << results_[i].p95_latency_ns << " ns" << std::endl;
            std::cout << "  P99 Latency: " << results_[i].p99_latency_ns << " ns" << std::endl;
        }
    }
};

#endif
