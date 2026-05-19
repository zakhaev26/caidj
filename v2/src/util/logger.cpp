#include "caidj/util/logger.hpp"

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <unordered_map>

namespace caidj::util
{
    namespace
    {

        std::mutex g_mutex;
        std::ofstream g_file;
        std::string g_level = "info";
        std::unordered_map<std::string, std::unique_ptr<SimpleLogger>> g_loggers;

        int level_rank(const std::string &level)
        {
            if (level == "trace" || level == "debug")
                return 0;
            if (level == "info")
                return 1;
            if (level == "warn")
                return 2;
            if (level == "error")
                return 3;
            return 1;
        }

        std::string timestamp()
        {
            const auto now = std::chrono::system_clock::now();
            const auto time = std::chrono::system_clock::to_time_t(now);
            std::tm tm{};
#if defined(_WIN32)
            localtime_s(&tm, &time);
#else
            localtime_r(&time, &tm);
#endif
            std::ostringstream oss;
            oss << std::put_time(&tm, "%H:%M:%S");
            return oss.str();
        }

    } // namespace

    SimpleLogger::SimpleLogger(std::string name) : name_(std::move(name)) {}

    void SimpleLogger::write(const std::string &level, const std::string &message)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (level_rank(level) < level_rank(g_level))
        {
            return;
        }
        const std::string line = "[" + timestamp() + "] [" + level + "] [" + name_ + "] " + message;
        std::cout << line << '\n';
        if (g_file)
        {
            g_file << line << '\n';
        }
    }

    void init_logger(const std::string &log_level, const std::string &log_file)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_level = log_level;
        const std::filesystem::path path(log_file);
        if (path.has_parent_path())
        {
            std::filesystem::create_directories(path.parent_path());
        }
        g_file.open(log_file, std::ios::app);
    }

    SimpleLogger *get_logger(const std::string &name)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_loggers.find(name);
        if (it != g_loggers.end())
        {
            return it->second.get();
        }
        auto logger = std::make_unique<SimpleLogger>(name);
        auto *ptr = logger.get();
        g_loggers.emplace(name, std::move(logger));
        return ptr;
    }

} // namespace caidj::util