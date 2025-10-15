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

#include "attribute/typed_attribute.hpp"
#include "core/feature.hpp"
#include "datadog/attribute.hpp"
#include "features/logging/types.hpp"

namespace datadog::impl {

/**
 * Internal state that's owned by a specific logger but shared with the Logging feature
 * for the purposes of generating log events.
 *
 * Since we don't guarantee thread safety for individual loggers, we can assume
 * exclusive access to this state in any API call initiated on the owning Logger.
 */
struct LoggerState {
  // Long-lived attribute values that are used in the final log event
  StringAttribute service_name;  // 'service'
  StringAttribute logger_name;   // 'logger.name'

  UUIDAttribute rum_application_id;  // 'application_id'; if enriched with RUM context
  UUIDAttribute rum_session_id;      // 'session_id'; if a RUM session is active
  UUIDAttribute rum_view_id;         // 'view.id'; if a RUM view is active
  UUIDAttribute rum_action_id;       // 'user_action.id'; if a RUM action is active

  // Intermediate objects used to hold key-value pairs to be merged into the final event
  // payload
  ObjectAttribute user_attributes;
  ObjectAttribute internal_attributes;

  // Reusable buffers for building the final event and serializing it to JSON
  ObjectAttribute event_object;
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

  void SetAttribute(std::string_view name, const Attribute& value);
  void DeleteAttribute(std::string_view name);

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
