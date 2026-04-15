// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/logging/logger.hpp"

#include "datadog/attribute.hpp"

#include "datadog/impl/core/util/assert.hpp"
#include "datadog/impl/core/version.hpp"

namespace datadog::impl {

LoggerState::LoggerState(
    const std::optional<std::string>& in_service_name,
    const std::optional<std::string>& in_logger_name,
    size_t initial_attribute_capacity
)
    : user_attributes(initial_attribute_capacity),
      event(
          in_service_name.value_or(""),
          in_logger_name.value_or(""),
          SDK_VERSION,
          initial_attribute_capacity
      ) {}

LoggerEnrichmentConfig::LoggerEnrichmentConfig(bool in_enable_rum)
    : enable_rum(in_enable_rum) {}

Logger::Logger(const LoggerConfig& config, const LogEventCallback& event_callback)
    : _min_level(config.remote_log_threshold),
      _sampling_rate_unit(config.remote_sample_rate / 100.0f),
      _sampling_rng(std::random_device{}()),
      _sampling_distribution(0.0f, 1.0f),
      _event_callback(event_callback),
      _state(config.service, config.name, config.initial_attribute_capacity),
      _enrichment(config.enrich_with_rum_context) {}

void Logger::AddAttribute(std::string_view name, const Attribute& value) {
  // Assume single-thread usage: don't synchronize access to attributes
  _state.user_attributes.attribute.SetObjectProperty(name, value);
}

void Logger::RemoveAttribute(std::string_view name) {
  // Assume single-thread usage: don't synchronize access to attributes
  _state.user_attributes.attribute.DeleteObjectProperty(name);
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
  _event_callback(_state, _enrichment, level, message, message_attributes);
}

}  // namespace datadog::impl
