// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#ifndef DATADOG_INCLUDE_LOGGING_H
#define DATADOG_INCLUDE_LOGGING_H

#include <stddef.h>

#include "datadog/api.h"
#include "datadog/attribute.h"
#include "datadog/core.h"

// These values establish the size of string buffers in the C API; they do not imply
// that the Datadog platform imposes any such limits
#define DATADOG_MAX_LOGGER_NAME_LEN 63

#ifdef __cplusplus
extern "C" {
#endif

// === Logger configuration ===

/**
 * Severity at which log messages may be emitted.
 */
typedef enum {
  DD_LOG_LEVEL_DEBUG,
  DD_LOG_LEVEL_INFO,
  DD_LOG_LEVEL_NOTICE,
  DD_LOG_LEVEL_WARN,
  DD_LOG_LEVEL_ERROR,
  DD_LOG_LEVEL_CRITICAL,
} dd_log_level_t;

/**
 * Logger configuration struct: initialize with dd_logger_config_init(), then call
 * dd_logger_config_set_<value>().
 */
typedef struct dd_logger_config {
  uint32_t version;
  float remote_sample_rate;
  char service[DATADOG_MAX_SERVICE_NAME_LEN + 1];
  char name[DATADOG_MAX_LOGGER_NAME_LEN + 1];
  dd_log_level_t remote_log_threshold;
  size_t initial_attribute_capacity;
} dd_logger_config_t;

/**
 * Initializes a dd_logger_config_t with default settings.
 */
DATADOG_API void dd_logger_config_init(dd_logger_config_t* config);

/**
 * Sets the remote sample rate to a value between 0.0 and 100.0, indicating what
 * percentage of log events should be sampled. At 100.0, all messages are sent to
 * intake; at 0.0, all messages are discarded. Default is 100.0.
 */
DATADOG_API void dd_logger_config_set_remote_sample_rate(
    dd_logger_config_t* config, float value
);

/**
 * Sets the service name to be used on messages emitted by a logger. If omitted, the
 * logger will use the service name configured globally via dd_core_config_t.
 *
 * Config stores a copy of provided string value. If the given string value exceeds
 * DATADOG_MAX_SERVICE_NAME_LEN, it will be truncated to that length. Both NULL and ""
 * will be interepreted as no value, causing the logger to use the default service name.
 */
DATADOG_API void dd_logger_config_set_service(
    dd_logger_config_t* config, const char* value
);

/**
 * Sets the name used to identify a logger in messages emitted by that logger. If
 * omitted, no 'logger.name' property will be present on log events.
 *
 * Config stores a copy of provided string value. If the given string value exceeds
 * DATADOG_MAX_LOGGER_NAME_LEN, it will be truncated to that length. Both NULL and ""
 * will be interpreted as no value.
 */
DATADOG_API void dd_logger_config_set_name(
    dd_logger_config_t* config, const char* value
);

/**
 * Sets the minimum log level at which messages will be sent to intake. Only messages at
 * or above this level will be considered for sampling; all messages below that level
 * will be dropped. Defaults to DD_LOG_LEVEL_DEBUG, meaning all messages will be sent to
 * intake.
 */
DATADOG_API void dd_logger_config_set_remote_log_threshold(
    dd_logger_config_t* config, dd_log_level_t value
);

/**
 * Sets the initial number of custom attributes for which memory will be preallocated on
 * logger creation. At the default of 0, does not reserve space for custom attributes.
 *
 * Custom attributes may be freely added beyond this limit. Setting an initial capacity
 * is simply a means of optimizing memory allocations based on expected usage.
 */
DATADOG_API void dd_logger_config_set_initial_attribute_capacity(
    dd_logger_config_t* config, size_t value
);

// === Logging feature interface ===

/**
 * Interface used to emit log messages.
 */
typedef struct dd_logger dd_logger_t;

/**
 * Interface to the Datadog SDK's logging feature. Use dd_logging_init() to register the
 * logging feature with the core. You MUST call dd_logging_destroy() when done.
 */
typedef struct dd_logging dd_logging_t;

/**
 * Registers the logging feature with the core of the Datadog SDK. MUST be matched with
 * a call to dd_logging_destroy().
 */
DATADOG_API dd_logging_t* dd_logging_init(dd_core_t* core);

/**
 * Frees all memory allocated for for the logging feature reference, rendering it no
 * longer usable. May be called at any time.
 */
DATADOG_API void dd_logging_destroy(dd_logging_t* logging);

/**
 * Adds or updates a global attribute value that will be included with all log messages
 * emitted by all loggers.
 */
DATADOG_API void dd_logging_attribute_set(
    dd_logging_t* logging, const char* name, const dd_attribute_t* value
);

/**
 * Removes a global attribute value, if one has been previously added with the given
 * name.
 */
DATADOG_API void dd_logging_attribute_delete(dd_logging_t* logging, const char* name);

// === Logger interface ===

/**
 * Creates and returns a new logger with the given configuration. You MUST call
 * dd_logger_destroy() when finished with the logger.
 *
 * If config is NULL, the default configuration values will be used.
 */
DATADOG_API dd_logger_t* dd_logger_create(
    dd_logging_t* logging, const dd_logger_config_t* config
);

/**
 * Frees all memory allocated for the given logger.
 */
DATADOG_API void dd_logger_destroy(dd_logger_t* logger);

/**
 * Adds or updates a logger-level attribute value that will be included with all
 * messages emitted by this logger. If a logger-level attribute shares its name with a
 * global attribute, the logger-level attribute will take precedence.
 */
DATADOG_API void dd_logger_attribute_set(
    dd_logger_t* logger, const char* name, const dd_attribute_t* value
);

/**
 * Removes a logger-level attribute value, if one has been previously added with the
 * given name.
 */
DATADOG_API void dd_logger_attribute_delete(dd_logger_t* logger, const char* name);

/**
 * Emits a log message at the given level.
 */
DATADOG_API void dd_logger_log(
    dd_logger_t* logger, dd_log_level_t level, const char* message
);
DATADOG_API void dd_logger_debug(dd_logger_t* logger, const char* message);
DATADOG_API void dd_logger_info(dd_logger_t* logger, const char* message);
DATADOG_API void dd_logger_notice(dd_logger_t* logger, const char* message);
DATADOG_API void dd_logger_warn(dd_logger_t* logger, const char* message);
DATADOG_API void dd_logger_error(dd_logger_t* logger, const char* message);
DATADOG_API void dd_logger_critical(dd_logger_t* logger, const char* message);

/**
 * Emits a log message at the given level, with the given set of message-level
 * attributes included.
 *
 * If attributes has type DD_VALUE_TYPE_OBJECT, each of its named values will be
 * included in the resulting log event, taking precedence over global and logger-level
 * attributes in case of name conflict. If attributes is a value of any other type, it
 * will be ignored.
 */
DATADOG_API void dd_logger_log_obj(
    dd_logger_t* logger,
    dd_log_level_t level,
    const char* message,
    const dd_attribute_t* attributes
);

DATADOG_API void dd_logger_info_obj(
    dd_logger_t* logger, const char* message, const dd_attribute_t* attributes
);

DATADOG_API void dd_logger_debug_obj(
    dd_logger_t* logger, const char* message, const dd_attribute_t* attributes
);

DATADOG_API void dd_logger_notice_obj(
    dd_logger_t* logger, const char* message, const dd_attribute_t* attributes
);

DATADOG_API void dd_logger_warn_obj(
    dd_logger_t* logger, const char* message, const dd_attribute_t* attributes
);

DATADOG_API void dd_logger_error_obj(
    dd_logger_t* logger, const char* message, const dd_attribute_t* attributes
);

DATADOG_API void dd_logger_critical_obj(
    dd_logger_t* logger, const char* message, const dd_attribute_t* attributes
);

#ifdef __cplusplus
}
#endif

#endif  // DATADOG_INCLUDE_LOGGING_H
