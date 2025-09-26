// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "features/logging/logger.hpp"

#include "assert.hpp"
#include "core/version.hpp"
#include "datadog/attribute.hpp"

namespace datadog::impl {

Logger::Logger(const LoggerConfig& config, const LogEventCallback& event_callback)
    : _min_level(config.remote_log_threshold),
      _sampling_rate_unit(config.remote_sample_rate / 100.0f),
      _sampling_rng(std::random_device{}()),
      _sampling_distribution(0.0f, 1.0f),
      _service_name(config.service),
      _logger_object(2),
      _logger_attributes(config.initial_attribute_capacity),
      _event_callback(event_callback),
      _event_object(config.initial_attribute_capacity + 8) {
  if (config.name) {
    _logger_object.attribute.SetObjectProperty("name", Attribute::String(*config.name));
  }
  _logger_object.attribute.SetObjectProperty("version", Attribute::String(SDK_VERSION));
}

void Logger::SetAttribute(std::string_view name, const Attribute& value) {
  _logger_attributes.attribute.SetObjectProperty(name, value);
}

void Logger::DeleteAttribute(std::string_view name) {
  _logger_attributes.attribute.DeleteObjectProperty(name);
}

void Logger::EmitLogEvent(
    LogLevel level, std::string_view message, const Attribute& message_attributes
) {
  // _event_callback should always be safe to call: it's initialized on Logger
  // construction and never cleared
  DATADOG_ASSERT(_event_callback != nullptr, "null event callback on EmitLogEvent");

  // Determine whether we should handle this message at all: if its severity level is
  // below the threshold for sampling, ignore it
  if (level < _min_level) {
    return;
  }

  // Similarly: if our sampling rate is zero, we ignore all messages
  if (_sampling_rate_unit <= 0.0f) {
    return;
  }

  // If we have a sampling rate below one, roll the dice to see if this message should
  // be excluded
  if (_sampling_rate_unit < 1.0f) {
    // Sampling rate defines percentage of messages to _keep_ (e.g. 0.9f => 90%
    // sampled), so discard if our roll is _above_ the threshold
    const float random_unit_value = _sampling_distribution(_sampling_rng);
    if (random_unit_value > _sampling_rate_unit) {
      return;
    }
  }

  // This message should be sampled: notify the logging feature implementation that it
  // should record this message
  _event_callback(
      _event_object.attribute,
      _event_buffer,
      _service_name,
      _logger_object,
      _logger_attributes.attribute,
      level,
      message,
      message_attributes
  );
}

}  // namespace datadog::impl
