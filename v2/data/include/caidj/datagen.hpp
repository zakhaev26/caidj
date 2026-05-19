#pragma once

#include "caidj/common.hpp"
#include "caidj/util/zipf.hpp"

#include <mutex>
#include <random>
#include <string>

namespace caidj
{

    class DataGen
    {
    public:
        DataGen(uint64_t seed, double zipf_alpha, int64_t domain_size);

        Relation generate_R(int64_t num_tuples);
        Relation generate_S(int64_t num_tuples);
        WriteOp generate_write_op(OpType op);

        static void save_csv(const Relation &rel, const std::string &path);
        static Relation load_csv(const std::string &path);

    private:
        Tuple make_tuple(TID tid);

        std::mutex mutex_;
        uint64_t seed_{};
        int64_t domain_size_{};
        util::ZipfSampler zipf_;
        std::mt19937_64 rng_;
    };

} // namespace caidj