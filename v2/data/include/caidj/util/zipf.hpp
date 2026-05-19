#pragma once

#include <cstdint>
#include <random>
#include <vector>

namespace caidj::util
{

    class ZipfSampler
    {
    public:
        ZipfSampler(double alpha, int64_t n, uint64_t seed);
        int64_t next();

    private:
        int64_t n_ = 1;
        std::mt19937_64 rng_;
        std::discrete_distribution<int64_t> distribution_;
    };

} // namespace caidj::util