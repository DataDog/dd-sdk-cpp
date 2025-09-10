#include "datadog/logging.h"

#include <memory>

#include "attribute/types.hpp"
#include "core/core.hpp"
#include "core_glue.hpp"
#include "datadog/core.h"
#include "features/logging/logger.hpp"
#include "features/logging/logging.hpp"
#include "features/logging/types.hpp"
#include "logging_glue.hpp"
#include "platform/clock.hpp"

static const dd_logger_config_t DEFAULT_LOGGER_CONFIG;

// NOLINTBEGIN(cppcoreguidelines-owning-memory)

extern "C" {

dd_logger_config_t* dd_logger_config_create(void) { return new dd_logger_config_t(); }

void dd_logger_config_destroy(dd_logger_config_t* config) { delete config; }

void dd_logger_config_set_remote_sample_rate(dd_logger_config_t* config, float value) {
  if (config) {
    config->cpp_config.SetRemoteSampleRate(value);
  }
}

void dd_logger_config_set_service(dd_logger_config_t* config, const char* value) {
  if (config && value) {
    config->cpp_config.SetService(value);
  }
}

void dd_logger_config_set_name(dd_logger_config_t* config, const char* value) {
  if (config && value) {
    config->cpp_config.SetName(value);
  }
}

void dd_logger_config_set_remote_log_threshold(
    dd_logger_config_t* config, dd_log_level_t value
) {
  if (config) {
    config->cpp_config.SetRemoteLogThreshold(datadog::LogLevel_FromC(value));
  }
}

void dd_logger_config_set_initial_attribute_capacity(
    dd_logger_config_t* config, size_t value
) {
  if (config) {
    config->cpp_config.SetInitialAttributeCapacity(value);
  }
}

dd_logging_t* dd_logging_init(dd_core_t* core) {
  // Don't crash if user fails to give us a valid core, but don't give them a no-op
  // implementation either
  if (!core || !core->impl) {
    return nullptr;
  }

  // Get essential state from the Core
  const datadog::platform::IClock& clock = core->impl->GetClock();
  std::string_view service_name = core->impl->GetServiceName();
  std::string_view application_version = core->impl->GetApplicationVersion();

  // Initialize our Logging feature implementation
  auto logging_impl = std::make_shared<datadog::impl::Logging>(
      clock, service_name, application_version
  );

  // Register the feature with the core, aborting on failure
  if (!core->impl->RegisterFeature(logging_impl)) {
    // TODO: Return a no-op interface
    return nullptr;
  }

  // Initialize and return the API object that represents our user-facing interface
  // for the logging feature
  dd_logging_t* logging = new dd_logging;
  logging->impl = std::move(logging_impl);
  return logging;
}

void dd_logging_destroy(dd_logging_t* logging) { delete logging; }

void dd_logging_attribute_set(
    dd_logging_t* logging, const char* name, const dd_attribute_t* value
) {
  // Abort if any argument is invalid (but allow empty-string as a property name)
  if (!logging || !name || !value) {
    return;
  }

  // Perform a lightweight copy to initialize a datadog::Attribute from our value, then
  // set it as a global attribute for the logging feature
  logging->impl->SetAttribute(
      name, datadog::impl::AttributeConversion::CopyFromC(*value)
  );
}

void dd_logging_attribute_delete(dd_logging_t* logging, const char* name) {
  if (!logging || !name) {
    return;
  }
  logging->impl->DeleteAttribute(name);
}

dd_logger_t* dd_logger_create(dd_logging_t* logging, const dd_logger_config_t* config) {
  if (!logging) {
    return nullptr;
  }

  if (!config) {
    config = &DEFAULT_LOGGER_CONFIG;
  }

  dd_logger_t* logger = new dd_logger;
  logger->impl = logging->impl->CreateLogger(config->cpp_config);
  return logger;
}

void dd_logger_destroy(dd_logger_t* logger) { delete logger; }

void dd_logger_attribute_set(
    dd_logger_t* logger, const char* name, const dd_attribute_t* value
) {
  // Abort if any argument is invalid (but allow empty-string as a property name)
  if (!logger || !name || !value) {
    return;
  }

  // Copy value to a datadog::Attribute and set the logger-level value
  logger->impl->SetAttribute(
      name, datadog::impl::AttributeConversion::CopyFromC(*value)
  );
}

void dd_logger_attribute_delete(dd_logger_t* logger, const char* name) {
  if (!logger || !name) {
    return;
  }
  logger->impl->DeleteAttribute(name);
}

void dd_logger_log(dd_logger_t* logger, dd_log_level_t level, const char* message) {
  // Abort if no logger or message provided (but still allow an empty-string message to
  // be logged)
  if (!logger || !message) {
    return;
  }

  logger->impl->EmitLogEvent(datadog::LogLevel_FromC(level), message);
}

void dd_logger_debug(dd_logger_t* logger, const char* message) {
  dd_logger_log(logger, DD_LOG_LEVEL_DEBUG, message);
}

void dd_logger_info(dd_logger_t* logger, const char* message) {
  dd_logger_log(logger, DD_LOG_LEVEL_INFO, message);
}

void dd_logger_notice(dd_logger_t* logger, const char* message) {
  dd_logger_log(logger, DD_LOG_LEVEL_NOTICE, message);
}

void dd_logger_warn(dd_logger_t* logger, const char* message) {
  dd_logger_log(logger, DD_LOG_LEVEL_WARN, message);
}

void dd_logger_error(dd_logger_t* logger, const char* message) {
  dd_logger_log(logger, DD_LOG_LEVEL_ERROR, message);
}

void dd_logger_critical(dd_logger_t* logger, const char* message) {
  dd_logger_log(logger, DD_LOG_LEVEL_CRITICAL, message);
}

void dd_logger_log_obj(
    dd_logger_t* logger, dd_log_level_t level, const char* message,
    const dd_attribute_t* attributes
) {
  // Abort if no logger or message provided (but still allow an empty-string message to
  // be logged)
  if (!logger || !message) {
    return;
  }

  // If we've been given a valid object attribute, convert it to the equivalent C++ type
  datadog::Attribute cpp_attribute;  // Default-initialized to Attribute::Null()
  if (attributes && attributes->type == DD_VALUE_TYPE_OBJECT) {
    cpp_attribute = datadog::impl::AttributeConversion::CopyFromC(*attributes);
  }

  // Emit a log event, passing our attribute value
  logger->impl->EmitLogEvent(datadog::LogLevel_FromC(level), message, cpp_attribute);
}

void dd_logger_info_obj(
    dd_logger_t* logger, const char* message, const dd_attribute_t* attributes
) {
  dd_logger_log_obj(logger, DD_LOG_LEVEL_INFO, message, attributes);
}

void dd_logger_debug_obj(
    dd_logger_t* logger, const char* message, const dd_attribute_t* attributes
) {
  dd_logger_log_obj(logger, DD_LOG_LEVEL_DEBUG, message, attributes);
}

void dd_logger_notice_obj(
    dd_logger_t* logger, const char* message, const dd_attribute_t* attributes
) {
  dd_logger_log_obj(logger, DD_LOG_LEVEL_NOTICE, message, attributes);
}

void dd_logger_warn_obj(
    dd_logger_t* logger, const char* message, const dd_attribute_t* attributes
) {
  dd_logger_log_obj(logger, DD_LOG_LEVEL_WARN, message, attributes);
}

void dd_logger_error_obj(
    dd_logger_t* logger, const char* message, const dd_attribute_t* attributes
) {
  dd_logger_log_obj(logger, DD_LOG_LEVEL_ERROR, message, attributes);
}

void dd_logger_critical_obj(
    dd_logger_t* logger, const char* message, const dd_attribute_t* attributes
) {
  dd_logger_log_obj(logger, DD_LOG_LEVEL_CRITICAL, message, attributes);
}
}

// NOLINTEND(cppcoreguidelines-owning-memory)
