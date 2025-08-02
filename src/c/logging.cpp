#include "datadog/logging.h"

#include "datadog/core.h"
#include "logging/types.hpp"
#include "logging/logging.hpp"

#include "core_glue.hpp"

#include <memory>

struct dd_logger
{
    std::unique_ptr<datadog::impl::Logger> impl;
};

struct dd_logging
{
    std::shared_ptr<datadog::impl::Logging> impl;
};

extern "C" {

dd_logging_t* dd_logging_init(dd_core_t* core)
{
    if (!core) return nullptr;
    if (!core->impl) return nullptr;

    try {
        auto impl = std::make_shared<datadog::impl::Logging>();
        if (!core->impl->RegisterFeature(impl))
        {
            return nullptr;
        }
        dd_logging_t* logging = new dd_logging;
        logging->impl = std::move(impl);
        return logging;
    } catch (...) {
        return nullptr;
    }
}

void dd_logging_destroy(dd_logging_t* logging)
{
    delete logging;
}

dd_logger_t* dd_logger_create(dd_logging_t* logging, const dd_logger_config_t* config)
{
    if (!logging) return nullptr;
    if (!config) return nullptr;

    datadog::LoggerConfig cpp_config = datadog::LoggerConfig_FromC(*config);
    dd_logger_t* logger = new dd_logger;
    logger->impl = logging->impl->CreateLogger(cpp_config);
    return logger;
}

void dd_logger_destroy(dd_logger_t* logger)
{
    delete logger;
}

void dd_logger_log(dd_logger_t* logger, dd_log_level_t level, const char* message)
{
    logger->impl->Log(datadog::LogLevel_FromC(level), message);
}

void dd_logger_debug(dd_logger_t* logger, const char* message)
{
    dd_logger_log(logger, DD_LOG_LEVEL_DEBUG, message);
}

void dd_logger_info(dd_logger_t* logger, const char* message)
{
    dd_logger_log(logger, DD_LOG_LEVEL_INFO, message);
}

void dd_logger_notice(dd_logger_t* logger, const char* message)
{
    dd_logger_log(logger, DD_LOG_LEVEL_NOTICE, message);
}

void dd_logger_warn(dd_logger_t* logger, const char* message)
{
    dd_logger_log(logger, DD_LOG_LEVEL_WARN, message);
}

void dd_logger_error(dd_logger_t* logger, const char* message)
{
    dd_logger_log(logger, DD_LOG_LEVEL_ERROR, message);
}

void dd_logger_critical(dd_logger_t* logger, const char* message)
{
    dd_logger_log(logger, DD_LOG_LEVEL_CRITICAL, message);
}

}
