#include "caidj/metrics/metrics.hpp"

#include <mutex>
#include <sstream>

namespace caidj::metrics
{

    MetricsCollector &MetricsCollector::instance()
    {
        static MetricsCollector collector;
        return collector;
    }

    void MetricsCollector::increment(const std::string &name, uint64_t by)
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        counters_[name] += by;
    }

    void MetricsCollector::gauge_set(const std::string &name, double value)
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        gauges_[name] = value;
    }

    uint64_t MetricsCollector::get_counter(const std::string &name) const
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        const auto it = counters_.find(name);
        return it == counters_.end() ? 0 : it->second;
    }

    double MetricsCollector::get_gauge(const std::string &name) const
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        const auto it = gauges_.find(name);
        return it == gauges_.end() ? 0.0 : it->second;
    }

    void MetricsCollector::reset_all()
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        counters_.clear();
        gauges_.clear();
    }

    std::string MetricsCollector::to_json() const
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        std::ostringstream oss;
        oss << "{\n  \"counters\": {";
        bool first = true;
        for (const auto &[name, value] : counters_)
        {
            if (!first)
                oss << ',';
            first = false;
            oss << "\n    \"" << name << "\": " << value;
        }
        oss << "\n  },\n  \"gauges\": {";
        first = true;
        for (const auto &[name, value] : gauges_)
        {
            if (!first)
                oss << ',';
            first = false;
            oss << "\n    \"" << name << "\": " << value;
        }
        oss << "\n  }\n}";
        return oss.str();
    }

} // namespace caidj::metrics