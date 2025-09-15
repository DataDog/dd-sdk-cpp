// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2024-Present Datadog, Inc.

#pragma once

#include <cinttypes>
#include <functional>
#include <random>
#include <shared_mutex>
#include <string>
#include <vector>

#include "attribute/typed_attribute.hpp"
#include "core/feature.hpp"
#include "datadog/attribute.hpp"
#include "features/logging/types.hpp"

namespace datadog::impl {

/**
 * Function used by a Logger to generate a new log message.
 */
using LogEventCallback = std::function<void(
    Attribute& mut_event_object, std::vector<uint8_t>& mut_event_buffer,
    const StringAttribute& logger_service_name, const ObjectAttribute& logger_object,
    const Attribute& logger_attributes, LogLevel level, std::string_view message,
    const Attribute& message_attributes
)>;

/**
 * Logger implementation. Owns its own logger-specific state, and handles the emitting
 * of log messages by relaying the requisite data to the logging feature implementation.
 */
class Logger {
 public:
  /**
   * Initializes a new logger with the given config.
   */
  explicit Logger(const LoggerConfig& config, const LogEventCallback& event_callback);

  void SetAttribute(std::string_view name, const Attribute& value);
  void DeleteAttribute(std::string_view name);

  /**
   * Handles a request to emit a single log message.
   */
  void EmitLogEvent(
      LogLevel level, std::string_view message,
      const Attribute& message_attributes = Attribute()
  );

 private:
  // Internal state used to make sampling decisions
  LogLevel _min_level;
  float _sampling_rate;
  std::mt19937 _sampling_rng;
  std::uniform_real_distribution<float> _sampling_distribution;

  // Long-lived attribute values that are referenced in the final log message
  StringAttribute _service_name;
  ObjectAttribute _logger_object;
  ObjectAttribute _logger_attributes;

  // Callback used to emit log messages via the centralized logging feature impl
  LogEventCallback _event_callback;

  // Logger-specific state
  ObjectAttribute _event_object;
  std::vector<uint8_t> _event_buffer;
};

}  // namespace datadog::impl
