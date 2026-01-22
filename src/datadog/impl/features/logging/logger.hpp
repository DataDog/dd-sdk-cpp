// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cinttypes>
#include <functional>
#include <random>
#include <shared_mutex>
#include <string>
#include <vector>

#include "datadog/attribute.hpp"

#include "datadog/impl/attribute/typed_attribute.hpp"
#include "datadog/impl/core/feature.hpp"
#include "datadog/impl/core/feature_types/logging.hpp"
#include "datadog/impl/features/logging/types.hpp"

namespace datadog::impl {

/**
 * Internal state that's owned by a specific logger but shared with the Logging feature
 * for the purposes of generating log events.
 *
 * Since we don't guarantee thread safety for individual loggers, we can assume
 * exclusive access to this state in any API call initiated on the owning Logger.
 */
struct LoggerState {
  // Current set of custom user attributes applied to this logger; to be merged with
  // global and log-event-level attributes
  ObjectAttribute user_attributes;

  // Working memory for storing the essential details of the log event prior to
  // serialization, along with the final set of merged user attributes for that event
  LogEvent event;
  ObjectAttribute merged_attributes;

  // Reusable buffer where the final JSON payload will be encoded prior to being
  // copied onto the storage thread's queue
  std::vector<uint8_t> event_buffer;

  explicit LoggerState(
      const std::optional<std::string>& in_service_name,
      const std::optional<std::string>& in_logger_name,
      size_t initial_attribute_capacity
  );
};

struct LoggerEnrichmentConfig {
  bool enable_rum;

  explicit LoggerEnrichmentConfig(bool in_enable_rum);
};

/**
 * Function used by a Logger to generate a new log message.
 */
using LogEventCallback = std::function<void(
    LoggerState& mut_state,
    const LoggerEnrichmentConfig& enrichment,
    LogLevel level,
    std::string_view message,
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

  void AddAttribute(std::string_view name, const Attribute& value);
  void RemoveAttribute(std::string_view name);

  /**
   * Handles a request to emit a single log message.
   */
  void EmitLogEvent(
      LogLevel level,
      std::string_view message,
      const Attribute& message_attributes = Attribute()
  );

 private:
  // Internal state used to make sampling decisions
  LogLevel _min_level;
  float _sampling_rate_unit;
  std::mt19937 _sampling_rng;
  std::uniform_real_distribution<float> _sampling_distribution;

  // State used when generating log events
  LogEventCallback _event_callback;
  LoggerState _state;
  LoggerEnrichmentConfig _enrichment;
};

}  // namespace datadog::impl
