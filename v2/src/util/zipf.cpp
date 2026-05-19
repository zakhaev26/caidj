#include "caidj/util/zipf.hpp"

#include <cmath>
#include <stdexcept>

namespace caidj::util
{

    ZipfSampler::ZipfSampler(double alpha, int64_t n, uint64_t seed) : n_(n), rng_(seed)
    {
        if (n <= 0)
        {
            throw std::invalid_argument("ZipfSampler domain must be positive");
        }
        std::vector<double> weights;
        weights.reserve(static_cast<size_t>(n_));
        for (int64_t i = 1; i <= n_; ++i)
        {
            weights.push_back(alpha <= 0.0 ? 1.0 : 1.0 / std::pow(static_cast<double>(i), alpha));
        }
        distribution_ = std::discrete_distribution<int64_t>(weights.begin(), weights.end());
    }

    int64_t ZipfSampler::next()
    {
        return distribution_(rng_) + 1;
    }

} // namespace caidj::util