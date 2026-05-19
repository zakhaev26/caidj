#pragma once

#include <cstdint>
#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace caidj::metrics
{

    class MetricsCollector
    {
    public:
        static MetricsCollector &instance();

        void increment(const std::string &name, uint64_t by = 1);
        void gauge_set(const std::string &name, double value);
        uint64_t get_counter(const std::string &name) const;
        double get_gauge(const std::string &name) const;
        void reset_all();
        std::string to_json() const;

    private:
        MetricsCollector() = default;

        mutable std::shared_mutex mutex_;
        std::unordered_map<std::string, uint64_t> counters_;
        std::unordered_map<std::string, double> gauges_;
    };

} // namespace caidj::metrics