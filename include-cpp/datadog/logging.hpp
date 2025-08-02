#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <optional>

namespace datadog {

// Forward declarations
namespace impl { struct Logger; }
namespace impl { struct Logging; }

enum class LogLevel
{
    Debug,
    Info,
    Notice,
    Warn,
    Error,
    Critical,
};

struct LoggerConfig
{
    float remote_sample_rate = 1.0f;
    std::optional<std::string> service;
    std::optional<std::string> name;
    LogLevel remote_log_threshold = LogLevel::Debug;
};

struct Logger
{
    void Log(LogLevel level, std::string_view message);

    inline void Debug(std::string_view message)
    {
        Log(LogLevel::Debug, message);
    }

    inline void Info(std::string_view message)
    {
        Log(LogLevel::Info, message);
    }

    inline void Notice(std::string_view message)
    {
        Log(LogLevel::Notice, message);
    }

    inline void Warn(std::string_view message)
    {
        Log(LogLevel::Warn, message);
    }

    inline void Error(std::string_view message)
    {
        Log(LogLevel::Error, message);
    }

    inline void Critical(std::string_view message)
    {
        Log(LogLevel::Critical, message);
    }

private:
    std::unique_ptr<impl::Logger> _impl;
};

struct Logging
{
    static std::shared_ptr<Logging> Register(struct Core& core);

private:
    std::shared_ptr<impl::Logging> _impl;
};

}
