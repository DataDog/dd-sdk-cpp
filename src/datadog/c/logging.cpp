// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/logging.h"

#include <cstdio>
#include <memory>
#include <string_view>

#include "datadog/core.h"

#include "datadog/c/core_glue.hpp"
#include "datadog/c/logging_glue.hpp"
#include "datadog/impl/core/attribute/types.hpp"
#include "datadog/impl/core/core.hpp"
#include "datadog/impl/core/platform/clock.hpp"
#include "datadog/impl/logging/logger.hpp"
#include "datadog/impl/logging/logging.hpp"
#include "datadog/impl/logging/types.hpp"

static const uint32_t LOGGER_CONFIG_VERSION = 1;

static const dd_logger_config_t DEFAULT_LOGGER_CONFIG = {
    LOGGER_CONFIG_VERSION,  // version
    100.0f,                 // remote_sample_rate
    "",                     // service
    "",                     // name
    DD_LOG_LEVEL_DEBUG,     // remote_log_threshold
    0,                      // initial_attribute_capacity
    true                    // enrich_with_rum_context
};

// This C API necessarily uses C-style idioms for memory management and strings.
// NOLINTBEGIN(cppcoreguidelines-owning-memory)
// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg)
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index)

extern "C" {

void dd_logger_config_init(dd_logger_config_t* config) {
  // Require an input value
  if (!config) {
    return;
  }
  *config = DEFAULT_LOGGER_CONFIG;
}

void dd_logger_config_set_remote_sample_rate(dd_logger_config_t* config, float value) {
  if (config) {
    config->remote_sample_rate = value;
  }
}

void dd_logger_config_set_service(dd_logger_config_t* config, const char* value) {
  if (config) {
    if (value) {
      std::snprintf(config->service, sizeof(config->service), "%s", value);
    } else {
      config->service[0] = '\0';
    }
  }
}

void dd_logger_config_set_name(dd_logger_config_t* config, const char* value) {
  if (config) {
    if (value) {
      std::snprintf(config->name, sizeof(config->name), "%s", value);
    } else {
      config->name[0] = '\0';
    }
  }
}

void dd_logger_config_set_remote_log_threshold(
    dd_logger_config_t* config, dd_log_level_t value
) {
  if (config) {
    config->remote_log_threshold = value;
  }
}

void dd_logger_config_set_initial_attribute_capacity(
    dd_logger_config_t* config, size_t value
) {
  if (config) {
    config->initial_attribute_capacity = value;
  }
}

void dd_logger_config_set_enrich_with_rum_context(
    dd_logger_config_t* config, bool value
) {
  if (config) {
    config->enrich_with_rum_context = value;
  }
}

dd_logging_t* dd_logging_init(dd_core_t* core) {
  // Require a valid core: note that in the C API, where we tolerate null arguments to
  // all API functions, returning NULL is effectively returning a no-op implementation
  if (!core || !core->impl) {
    return nullptr;
  }

  // Get essential state from the Core
  const datadog::platform::IClock& clock = core->impl->GetClock();

  // Initialize our Logging feature implementation
  auto logging_impl = std::make_shared<datadog::impl::Logging>(clock);

  // Register the feature with the core, returning a no-op interface on failure
  if (!core->impl->RegisterFeature(logging_impl)) {
    return nullptr;
  }

  // Initialize and return the API object that represents our user-facing interface
  // for the logging feature
  return new dd_logging(std::move(logging_impl), core->diagnostic_logger);
}

void dd_logging_destroy(dd_logging_t* logging) { delete logging; }

void dd_logging_add_attribute(
    dd_logging_t* logging, const char* name, const dd_attribute_t* value
) {
  // Permit no-op calls
  if (!logging || !logging->impl) {
    return;
  }

  // Require a valid string, but allow "" as an attribute name
  if (!name) {
    logging->diagnostic_logger.Warning(
        "dd_logging_add_attribute call ignored: application must supply an attribute "
        "name"
    );
    return;
  }

  // Require a valid attribute value
  if (!value) {
    logging->diagnostic_logger.Warning(
        "dd_logging_add_attribute call ignored: application must supply an attribute "
        "value"
    );
    return;
  }

  // Perform a lightweight copy to initialize a datadog::Attribute from our value, then
  // set it as a global attribute for the logging feature
  logging->impl->AddAttribute(
      name, datadog::impl::AttributeConversion::CopyFromC(*value)
  );
}

void dd_logging_remove_attribute(dd_logging_t* logging, const char* name) {
  // Permit no-op calls
  if (!logging || !logging->impl) {
    return;
  }

  // Require a valid string, but allow "" as an attribute name
  if (!name) {
    logging->diagnostic_logger.Warning(
        "dd_logging_remove_attribute call ignored: application must supply an "
        "attribute name"
    );
    return;
  }

  logging->impl->RemoveAttribute(name);
}

dd_logger_t* dd_logger_create(dd_logging_t* logging, const dd_logger_config_t* config) {
  // Permit no-op calls without a warning
  if (!logging || !logging->impl) {
    return nullptr;
  }

  // If no config was given, use the default config
  if (!config) {
    config = &DEFAULT_LOGGER_CONFIG;
  }

  // If we were given a config struct with an invalid version number, assume it was not
  // properly initialized and fall back to the default config
  if (config->version <= 0 || config->version > LOGGER_CONFIG_VERSION) {
    logging->diagnostic_logger.Warning(
        "dd_logger_create falling back to default config: application must initialize "
        "dd_logger_config_t value via dd_logger_config_init"
    );
    config = &DEFAULT_LOGGER_CONFIG;
  }

  // Convert from dd_logger_config_t to datadog::LoggerConfig and create our logger
  const auto cpp_config = datadog::LoggerConfig_FromC(*config);
  return new dd_logger(
      logging->impl->CreateLogger(cpp_config), logging->diagnostic_logger
  );
}

void dd_logger_destroy(dd_logger_t* logger) { delete logger; }

void dd_logger_add_attribute(
    dd_logger_t* logger, const char* name, const dd_attribute_t* value
) {
  // Permit no-op calls
  if (!logger || !logger->impl) {
    return;
  }

  // Require a valid string, but allow "" as an attribute name
  if (!name) {
    logger->diagnostic_logger.Warning(
        "dd_logger_add_attribute call ignored: application must supply an attribute "
        "name"
    );
    return;
  }

  // Require a valid attribute value
  if (!value) {
    logger->diagnostic_logger.Warning(
        "dd_logger_add_attribute call ignored: application must supply an attribute "
        "value"
    );
    return;
  }

  // Copy value to a datadog::Attribute and set the logger-level value
  logger->impl->AddAttribute(
      name, datadog::impl::AttributeConversion::CopyFromC(*value)
  );
}

void dd_logger_remove_attribute(dd_logger_t* logger, const char* name) {
  // Permit no-op calls
  if (!logger || !logger->impl) {
    return;
  }

  // Require a valid string, but allow "" as an attribute name
  if (!name) {
    logger->diagnostic_logger.Warning(
        "dd_logger_remove_attribute call ignored: application must supply an attribute "
        "name"
    );
    return;
  }

  logger->impl->RemoveAttribute(name);
}

void dd_logger_add_tag(dd_logger_t* logger, const char* tag) {
  // Permit no-op calls
  if (!logger || !logger->impl) {
    return;
  }

  // Allow empty strings: all validation logic is handled within impl::Logger, using the
  // DiagnosticLogger that we provide
  const std::string_view cpp_tag = tag ? std::string_view{tag} : std::string_view{};
  logger->impl->AddTag(cpp_tag, logger->diagnostic_logger);
}

void dd_logger_add_tag_kv(dd_logger_t* logger, const char* key, const char* value) {
  if (!logger || !logger->impl) {
    return;
  }
  const std::string_view cpp_key = key ? std::string_view{key} : std::string_view{};
  const std::string_view cpp_value =
      value ? std::string_view{value} : std::string_view{};
  logger->impl->AddTag(cpp_key, cpp_value, logger->diagnostic_logger);
}

void dd_logger_remove_tag(dd_logger_t* logger, const char* tag) {
  if (!logger || !logger->impl) {
    return;
  }
  const std::string_view cpp_tag = tag ? std::string_view{tag} : std::string_view{};
  logger->impl->RemoveTag(cpp_tag);
}

void dd_logger_remove_tags_with_key(dd_logger_t* logger, const char* key) {
  if (!logger || !logger->impl) {
    return;
  }
  const std::string_view cpp_key = key ? std::string_view{key} : std::string_view{};
  logger->impl->RemoveTagsWithKey(cpp_key);
}

void dd_logger_log(
    dd_logger_t* logger,
    dd_log_level_t level,
    const char* message,
    const dd_log_error_t* err,
    const dd_attribute_t* attributes
) {
  // Allow no-op function calls on a null logger
  if (!logger || !logger->impl) {
    return;
  }

  // Abort if no message provided (but still allow an empty-string message to be logged)
  if (!message) {
    logger->diagnostic_logger.Warning(
        "dd_logger_log call ignored: application must supply a message"
    );
    return;
  }

  // If we've been given an error details struct, convert it to the equivalent C++ type
  datadog::LogError cpp_err{};
  if (err) {
    cpp_err = datadog::LogError_FromC(*err);
  }

  // If we've been given a valid object attribute, convert it to the equivalent C++ type
  datadog::Attribute cpp_attribute;  // Default-initialized to Attribute::Null()
  if (attributes && attributes->type == DD_VALUE_TYPE_OBJECT) {
    cpp_attribute = datadog::impl::AttributeConversion::CopyFromC(*attributes);
  }

  // Emit a log event, passing our attribute value
  logger->impl->Log(datadog::LogLevel_FromC(level), message, cpp_err, cpp_attribute);
}

void dd_logger_debug(
    dd_logger_t* logger,
    const char* message,
    const dd_log_error_t* err,
    const dd_attribute_t* attributes
) {
  dd_logger_log(logger, DD_LOG_LEVEL_DEBUG, message, err, attributes);
}

void dd_logger_info(
    dd_logger_t* logger,
    const char* message,
    const dd_log_error_t* err,
    const dd_attribute_t* attributes
) {
  dd_logger_log(logger, DD_LOG_LEVEL_INFO, message, err, attributes);
}

void dd_logger_notice(
    dd_logger_t* logger,
    const char* message,
    const dd_log_error_t* err,
    const dd_attribute_t* attributes
) {
  dd_logger_log(logger, DD_LOG_LEVEL_NOTICE, message, err, attributes);
}

void dd_logger_warn(
    dd_logger_t* logger,
    const char* message,
    const dd_log_error_t* err,
    const dd_attribute_t* attributes
) {
  dd_logger_log(logger, DD_LOG_LEVEL_WARN, message, err, attributes);
}

void dd_logger_error(
    dd_logger_t* logger,
    const char* message,
    const dd_log_error_t* err,
    const dd_attribute_t* attributes
) {
  dd_logger_log(logger, DD_LOG_LEVEL_ERROR, message, err, attributes);
}

void dd_logger_critical(
    dd_logger_t* logger,
    const char* message,
    const dd_log_error_t* err,
    const dd_attribute_t* attributes
) {
  dd_logger_log(logger, DD_LOG_LEVEL_CRITICAL, message, err, attributes);
}
}

// NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
// NOLINTEND(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
// NOLINTEND(cppcoreguidelines-pro-type-vararg)
// NOLINTEND(cppcoreguidelines-owning-memory)
