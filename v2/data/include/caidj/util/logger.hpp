#pragma once

#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>

namespace caidj::util
{

    class SimpleLogger
    {
    public:
        explicit SimpleLogger(std::string name);

        template <typename... Args>
        void info(const std::string &message, Args &&...args)
        {
            log("info", message, std::forward<Args>(args)...);
        }

        template <typename... Args>
        void debug(const std::string &message, Args &&...args)
        {
            log("debug", message, std::forward<Args>(args)...);
        }

        template <typename... Args>
        void warn(const std::string &message, Args &&...args)
        {
            log("warn", message, std::forward<Args>(args)...);
        }

        template <typename... Args>
        void error(const std::string &message, Args &&...args)
        {
            log("error", message, std::forward<Args>(args)...);
        }

    private:
        template <typename... Args>
        void log(const std::string &level, const std::string &message, Args &&...args)
        {
            std::ostringstream oss;
            oss << message;
            ((oss << ' ' << std::forward<Args>(args)), ...);
            write(level, oss.str());
        }

        void write(const std::string &level, const std::string &message);

        std::string name_;
    };

    void init_logger(const std::string &log_level, const std::string &log_file = "logs/caidj.log");
    SimpleLogger *get_logger(const std::string &name);

} // namespace caidj::util

#define LOG_TRACE(logger, ...)            \
    do                                    \
    {                                     \
        if ((logger) != nullptr)          \
        {                                 \
            (logger)->debug(__VA_ARGS__); \
        }                                 \
    } while (false)
#define LOG_DEBUG(logger, ...)            \
    do                                    \
    {                                     \
        if ((logger) != nullptr)          \
        {                                 \
            (logger)->debug(__VA_ARGS__); \
        }                                 \
    } while (false)
#define LOG_INFO(logger, ...)            \
    do                                   \
    {                                    \
        if ((logger) != nullptr)         \
        {                                \
            (logger)->info(__VA_ARGS__); \
        }                                \
    } while (false)
#define LOG_WARN(logger, ...)            \
    do                                   \
    {                                    \
        if ((logger) != nullptr)         \
        {                                \
            (logger)->warn(__VA_ARGS__); \
        }                                \
    } while (false)
#define LOG_ERROR(logger, ...)            \
    do                                    \
    {                                     \
        if ((logger) != nullptr)          \
        {                                 \
            (logger)->error(__VA_ARGS__); \
        }                                 \
    } while (false)