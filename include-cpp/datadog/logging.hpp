#pragma once

#include <cinttypes>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace datadog {

// Forward declarations
namespace impl {
class Logger;
class Logging;
}

enum class LogLevel : uint8_t
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

class Logger
{
public:
    void Log(LogLevel level, std::string_view message);

    void Debug(std::string_view message)
    {
        Log(LogLevel::Debug, message);
    }

    void Info(std::string_view message)
    {
        Log(LogLevel::Info, message);
    }

    void Notice(std::string_view message)
    {
        Log(LogLevel::Notice, message);
    }

    void Warn(std::string_view message)
    {
        Log(LogLevel::Warn, message);
    }

    void Error(std::string_view message)
    {
        Log(LogLevel::Error, message);
    }

    void Critical(std::string_view message)
    {
        Log(LogLevel::Critical, message);
    }

private:
    std::unique_ptr<impl::Logger> _impl;
};

class Logging
{
public:
    static std::shared_ptr<Logging> Register(class Core& core);

private:
    std::shared_ptr<impl::Logging> _impl;
};

} // namespace datadog
