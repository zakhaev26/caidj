#include <iostream>
#include <thread>
#include <chrono>
#include <random>
#include <atomic>
#include <vector>
#include <algorithm>
#include <numeric>
#include <iomanip>
#include <sstream>

#include "../include/types.hpp"
#include "../include/epoch_reclamation.hpp"
#include "../include/concurrent_index.hpp"
#include "../include/skew_aware_join.hpp"
#include "../include/baseline_index.hpp"
#include "../include/baseline_join.hpp"
#include "../include/benchmark.hpp"

void printUsage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --threads N       Number of threads (default: 4)" << std::endl;
    std::cout << "  --size N         Dataset size (default: 1000000)" << std::endl;
    std::cout << "  --skew F         Skew factor 0.0-1.0 (default: 0.0)" << std::endl;
    std::cout << "  --baseline       Run baseline only" << std::endl;
    std::cout << "  --concurrent     Run concurrent only" << std::endl;
    std::cout << "  --skew-aware     Run skew-aware only" << std::endl;
    std::cout << "  --all            Run all benchmarks (default)" << std::endl;
    std::cout << "  --scalability    Run scalability test" << std::endl;
    std::cout << "  --help           Show this help" << std::endl;
}

int main(int argc, char* argv[]) {
    BenchmarkRunner::BenchmarkConfig config;
    
    bool run_scalability = false;
    bool baseline_only = false;
    bool concurrent_only = false;
    bool skew_aware_only = false;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--threads" && i + 1 < argc) {
            config.num_threads = std::stoi(argv[++i]);
        } else if (arg == "--size" && i + 1 < argc) {
            config.dataset_size = std::stoull(argv[++i]);
        } else if (arg == "--skew" && i + 1 < argc) {
            config.skew_factor = std::stod(argv[++i]);
        } else if (arg == "--baseline") {
            baseline_only = true;
            config.run_concurrent = false;
            config.run_skew_aware = false;
        } else if (arg == "--concurrent") {
            concurrent_only = true;
            config.run_baseline = false;
            config.run_skew_aware = false;
        } else if (arg == "--skew-aware") {
            skew_aware_only = true;
            config.run_baseline = false;
            config.run_concurrent = false;
        } else if (arg == "--all") {
            config.run_baseline = true;
            config.run_concurrent = true;
            config.run_skew_aware = true;
        } else if (arg == "--scalability") {
            run_scalability = true;
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        }
    }
    
    std::cout << "==================================================" << std::endl;
    std::cout << "Concurrency-Aware Indexing Benchmark" << std::endl;
    std::cout << "==================================================" << std::endl;
    std::cout << std::endl;
    
    EpochReclamation::getInstance().initialize();
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    if (run_scalability) {
        BenchmarkRunner runner(config);
        runner.runScalabilityTest();
    } else {
        BenchmarkRunner runner(config);
        runner.runAllBenchmarks();
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto total_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    
    std::cout << "\nTotal execution time: " << total_time << " ms" << std::endl;
    std::cout << "==================================================" << std::endl;
    
    EpochReclamation::getInstance().shutdown();
    
    return 0;
}
