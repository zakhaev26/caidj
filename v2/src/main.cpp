#include "caidj/bench/benchmark.hpp"
#include "caidj/config.hpp"
#include "caidj/datagen.hpp"
#include "caidj/util/logger.hpp"

#include <exception>
#include <filesystem>
#include <iostream>

namespace
{

    void print_help()
    {
        std::cout << R"(caidj [OPTIONS]

OPTIONS:
  --config <path>           Path to TOML config file (default: configs/default.toml)
  --protocol <name>         nhj|echi|mpimvcc|bfcsi|all (default: all)
  --concurrency <c>         Run only at this writer count
  --r-size <n>              Override R relation size
  --s-size <n>              Override S relation size
  --seed <n>                Override random seed
  --output <path>           Output directory (default: results/)
  --runs <n>                Number of repetitions
  --duration <ms>           Trial duration in ms
  --probe-threads <n>       Number of join probe threads
  --generate-only           Generate data/relation_R.csv and data/relation_S.csv only
  --no-csv                  Skip CSV output
  --log-level <level>       trace|debug|info|warn|error
  --help                    Print this help and exit
  --version                 Print version string and exit
)";
    }

} // namespace

int main(int argc, char **argv)
{
    try
    {
        caidj::Config cfg = caidj::Config::from_args(argc, argv);
        if (cfg.show_help)
        {
            print_help();
            return 0;
        }
        if (cfg.show_version)
        {
            std::cout << "CAIDJ 1.0.0\n";
            return 0;
        }

        caidj::util::init_logger(cfg.log_level);
        auto *logger = caidj::util::get_logger("main");
        LOG_INFO(logger, "CAIDJ v1.0.0 starting");

        if (cfg.generate_only)
        {
            caidj::DataGen gen(cfg.seed, cfg.zipf_alpha, cfg.domain_size);
            std::filesystem::create_directories("data");
            caidj::DataGen::save_csv(gen.generate_R(cfg.r_size), "data/relation_R.csv");
            caidj::DataGen::save_csv(gen.generate_S(cfg.s_size), "data/relation_S.csv");
            std::cout << "Generated data/relation_R.csv and data/relation_S.csv\n";
            return 0;
        }

        caidj::bench::Benchmark benchmark(cfg);
        const auto results = benchmark.run_all();
        benchmark.save_results(results, cfg.output_dir);
        caidj::bench::print_summary_table(results);
        const std::filesystem::path out_dir(cfg.output_dir);
        std::cout << "Results saved to " << (out_dir / "results.json").string()
                  << " and " << (out_dir / "results.csv").string() << '\n';
        return 0;
    }
    catch (const std::exception &ex)
    {
        std::cerr << "caidj: error: " << ex.what() << '\n';
        return 1;
    }
}