#pragma once

#include "caidj/common.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace caidj {

struct Config {
    uint64_t seed = 42;
    double zipf_alpha = 1.2;
    int64_t domain_size = 200'000;
    int64_t r_size = 150'000;
    int64_t s_size = 50'000;

    int num_runs = 3;
    uint64_t trial_duration_ms = 30'000;
    int num_probe_threads = 10;
    std::vector<int> concurrency_levels{1, 2, 4, 8, 16};
    std::vector<Protocol> protocols{Protocol::NHJ, Protocol::ECHI, Protocol::MPI_MVCC, Protocol::BF_CSI};

    std::string output_dir = "results/";
    bool write_csv = true;
    bool write_json = true;
    std::string log_level = "info";

    size_t echi_delta_threshold = 1000;
    uint64_t echi_epoch_interval_ms = 100;

    uint64_t mpimvcc_gc_interval_ms = 500;

    double bfcsi_fpr = 0.01;
    double bfcsi_rebuild_threshold = 0.20;
    int bfcsi_num_shards = 16;
    size_t bfcsi_fp_cache_capacity = 4096;

    double insert_fraction = 0.80;

    bool generate_only = false;
    bool show_help = false;
    bool show_version = false;
    std::string config_path = "configs/default.toml";

    static Config from_toml(const std::string& path);
    static Config from_args(int argc, char** argv);
    void validate() const;
};

std::string config_to_json(const Config& cfg);

} // namespace caidj