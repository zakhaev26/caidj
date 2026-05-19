#include "caidj/bench/benchmark.hpp"
#include "caidj/config.hpp"
#include "caidj/datagen.hpp"
#include "caidj/index/base_index.hpp"
#include "caidj/index/bfcsi_index.hpp"
#include "caidj/index/echi_index.hpp"
#include "caidj/index/mpimvcc_index.hpp"
#include "caidj/index/nhj_index.hpp"
#include "caidj/join/join_executor.hpp"
#include "caidj/metrics/metrics.hpp"
#include "caidj/util/bloom_filter.hpp"
#include "caidj/util/skiplist.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{

    int failures = 0;

    void expect_true(bool condition, const std::string &message)
    {
        if (!condition)
        {
            ++failures;
            std::cerr << "[FAIL] " << message << '\n';
        }
    }

    void expect_eq(uint64_t a, uint64_t b, const std::string &message)
    {
        expect_true(a == b, message + " expected=" + std::to_string(b) + " actual=" + std::to_string(a));
    }

    caidj::Relation sample_relation()
    {
        return {{0, 1, 0, 0}, {1, 1, 0, 0}, {2, 2, 0, 0}, {3, 3, 0, 0}};
    }

    void test_datagen()
    {
        caidj::DataGen a(42, 1.2, 100);
        caidj::DataGen b(42, 1.2, 100);
        const auto r1 = a.generate_R(100);
        const auto r2 = b.generate_R(100);
        expect_eq(r1.size(), 100, "DataGen generates requested size");
        expect_true(r1 == r2, "DataGen is deterministic for the same seed");
        expect_true(std::all_of(r1.begin(), r1.end(), [](const caidj::Tuple &t)
                                { return t.key >= 1 && t.key <= 100; }),
                    "DataGen keys stay in range");
        std::filesystem::create_directories("build/test_data");
        caidj::DataGen::save_csv(r1, "build/test_data/relation.csv");
        const auto loaded = caidj::DataGen::load_csv("build/test_data/relation.csv");
        expect_true(r1 == loaded, "DataGen CSV round trip");
    }

    void test_bloom_filter()
    {
        caidj::util::BloomFilter bf(1000, 0.01);
        for (int i = 1; i <= 1000; ++i)
        {
            bf.insert(i);
        }
        bool all_present = true;
        for (int i = 1; i <= 1000; ++i)
        {
            all_present = all_present && bf.possibly_present(i);
        }
        expect_true(all_present, "BloomFilter has no false negatives for inserted keys");
    }

    void test_skiplist()
    {
        caidj::util::ConcurrentSkipList list;
        list.insert(7, 70);
        list.insert(7, 71);
        auto tids = list.lookup(7);
        std::sort(tids.begin(), tids.end());
        expect_true((tids == std::vector<caidj::TID>{70, 71}), "SkipList handles duplicate keys");
        list.remove(7, 70);
        tids = list.lookup(7);
        expect_true((tids == std::vector<caidj::TID>{71}), "SkipList removes one TID");
    }

    void test_index(caidj::Protocol protocol)
    {
        caidj::Config cfg;
        cfg.r_size = 4;
        cfg.s_size = 4;
        cfg.echi_epoch_interval_ms = 0;
        cfg.mpimvcc_gc_interval_ms = 0;
        auto idx = caidj::index::make_index(protocol, cfg);
        idx->bulk_load(sample_relation());
        expect_eq(idx->probe(1).size(), 2, caidj::protocol_to_string(protocol) + " bulk_load/probe");
        idx->insert(4, 40);
        if (protocol == caidj::Protocol::ECHI)
        {
            auto *echi = dynamic_cast<caidj::index::ECHIIndex *>(idx.get());
            echi->trigger_epoch_transition();
        }
        expect_eq(idx->probe(4).size(), 1, caidj::protocol_to_string(protocol) + " insert/probe");
        idx->remove(4, 40);
        if (protocol == caidj::Protocol::ECHI)
        {
            auto *echi = dynamic_cast<caidj::index::ECHIIndex *>(idx.get());
            echi->trigger_epoch_transition();
        }
        expect_true(idx->probe(4).empty(), caidj::protocol_to_string(protocol) + " remove/probe");
    }

    void test_join_executor()
    {
        caidj::Config cfg;
        auto idx = caidj::index::make_index(caidj::Protocol::NHJ, cfg);
        idx->bulk_load({{0, 1, 0, 0}, {1, 1, 0, 0}, {2, 2, 0, 0}});
        caidj::Relation s{{100, 1, 0, 0}, {101, 2, 0, 0}, {102, 3, 0, 0}};
        caidj::join::JoinExecutor executor(idx.get(), 2, s);
        const auto stats = executor.execute();
        expect_eq(stats.total_probes, 3, "JoinExecutor probe count");
        expect_eq(stats.total_result_pairs, 3, "JoinExecutor result pair count");
    }

    void test_metrics()
    {
        auto &m = caidj::metrics::MetricsCollector::instance();
        m.reset_all();
        constexpr int threads = 8;
        constexpr int increments = 1000;
        std::vector<std::thread> workers;
        for (int i = 0; i < threads; ++i)
        {
            workers.emplace_back([&m]()
                                 {
            for (int j = 0; j < increments; ++j) {
                m.increment("x");
            } });
        }
        for (auto &t : workers)
        {
            t.join();
        }
        expect_eq(m.get_counter("x"), threads * increments, "Metrics atomic increment");
        m.reset_all();
        expect_eq(m.get_counter("x"), 0, "Metrics reset");
    }

    void test_benchmark_smoke()
    {
        caidj::Config cfg;
        cfg.r_size = 100;
        cfg.s_size = 50;
        cfg.domain_size = 50;
        cfg.num_runs = 1;
        cfg.num_probe_threads = 2;
        cfg.trial_duration_ms = 10;
        cfg.concurrency_levels = {1};
        cfg.protocols = {caidj::Protocol::NHJ};
        cfg.output_dir = "build/test_results";
        caidj::bench::Benchmark bench(cfg);
        const auto results = bench.run_all();
        expect_eq(results.size(), 1, "Benchmark smoke result count");
        bench.save_results(results, cfg.output_dir);
        expect_true(std::filesystem::exists("build/test_results/results.json"), "Benchmark writes JSON");
    }

} // namespace

int main()
{
    test_datagen();
    test_bloom_filter();
    test_skiplist();
    test_index(caidj::Protocol::NHJ);
    test_index(caidj::Protocol::ECHI);
    test_index(caidj::Protocol::MPI_MVCC);
    test_index(caidj::Protocol::BF_CSI);
    test_join_executor();
    test_metrics();
    test_benchmark_smoke();

    if (failures == 0)
    {
        std::cout << "All CAIDJ tests passed\n";
        return EXIT_SUCCESS;
    }
    std::cerr << failures << " CAIDJ test(s) failed\n";
    return EXIT_FAILURE;
}