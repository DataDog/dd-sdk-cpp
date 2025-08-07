#ifndef DATADOG_INCLUDE_LOGGING_H
#define DATADOG_INCLUDE_LOGGING_H

#include "datadog/core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    DD_LOG_LEVEL_DEBUG,
    DD_LOG_LEVEL_INFO,
    DD_LOG_LEVEL_NOTICE,
    DD_LOG_LEVEL_WARN,
    DD_LOG_LEVEL_ERROR,
    DD_LOG_LEVEL_CRITICAL,
} dd_log_level_t;

typedef struct dd_logger_config
{
    float remote_sample_rate;
    const char* service;
    const char* name;
    dd_log_level_t remote_log_threshold;
} dd_logger_config_t;

typedef struct dd_logger dd_logger_t;
typedef struct dd_logging dd_logging_t;

dd_logging_t* dd_logging_init(dd_core_t* core);
void dd_logging_destroy(dd_logging_t* logging);

dd_logger_t* dd_logger_create(dd_logging_t* logging, const dd_logger_config_t* config);
void dd_logger_destroy(dd_logger_t* logger);

void dd_logger_log(dd_logger_t* log, dd_log_level_t level, const char* message);
void dd_logger_debug(dd_logger_t* log, const char* message);
void dd_logger_info(dd_logger_t* log, const char* message);
void dd_logger_notice(dd_logger_t* log, const char* message);
void dd_logger_warn(dd_logger_t* log, const char* message);
void dd_logger_error(dd_logger_t* log, const char* message);
void dd_logger_critical(dd_logger_t* log, const char* message);

#ifdef __cplusplus
}
#endif

#endif // DATADOG_INCLUDE_LOGGING_H
