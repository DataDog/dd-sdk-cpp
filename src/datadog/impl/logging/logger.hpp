// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cinttypes>
#include <memory>
#include <random>
#include <string_view>
#include <vector>

#include "datadog/attribute.hpp"
#include "datadog/logging.hpp"

#include "datadog/impl/core/feature_scope.hpp"
#include "datadog/impl/logging/data.hpp"

namespace datadog::impl {

class Logging;

/**
 * Implementation-layer Logger object, wrapped at the API layer by `datadog::Logger` and
 * `dd_logger_t`.
 *
 * Maintains a weak reference to the `Logging` feature for access to global state, and
 * handles `Log` calls from the application by making sampling decisions on the calling
 * thread, then enqueuing context-thread callbacks which will ultimately produce
 * JSON-encoded `LogEvent` values for storage.
 *
 * As currently implemented, `Logger` is not designed to be thread-safe: the application
 * developer is advised that any given Logger should only be used from one thread at a
 * time. It _is_ safe to create and use multiple Loggers across different threads, so
 * long as each Logger is confined to a single thread at a time.
 */
class Logger {
 public:
  explicit Logger(const LoggerConfig& config, std::weak_ptr<Logging> logging);

  /**
   * Adds or updates a logger-level attribute, which will be included in log events
   * emitted by this logger.
   */
  void AddAttribute(std::string_view key, const Attribute& value);

  /**
   * Removes a logger-level attribute, if any exists with the given name.
   */
  void RemoveAttribute(std::string_view key);

  /**
   * Records a single Log call from the application, with the given log level, message
   * text, and set of custom log-level attribute values.
   *
   * If `level` meets the configured threshold and a sampling decision passes, a
   * context-thread callback will be enqueued, causing a `LogEvent` to be generated and
   * flushed to disk in response to this call.
   *
   * `message` is a view of the application-provided string value, whose ownership and
   * lifetime we do not control. If the remote sampling decision passes, we will make a
   * copy of this string.
   *
   * Any values given in `attributes` whose names match existing logger-level or global
   * logging attributes will take precedence, shadowing those earlier values.
   */
  void Log(
      LogLevel level,
      std::string_view message,
      const Attribute& attributes = Attribute()
  ) const;

 private:
  /**
   * Determines whether the logger should generate a remote log event in response to a
   * new log call. Applies the configured log level threshold, making a random sampling
   * decision if needed.
   */
  bool ShouldSendLogEvent(LogLevel level) const;

  std::weak_ptr<Logging> _logging;  // Reference to Logging feature implementation
  Attribute _attributes;            // Logger-level attributes applied to all messages

  // Immutable snapshot of configuration details that affect log messages
  std::shared_ptr<const LoggerConfigDetails> _details;

  // Sampling state used in the caller thread
  LogLevel _remote_log_threshold;
  float _remote_sample_rate_unit;
  mutable std::mt19937 _sampling_rng;
  mutable std::uniform_real_distribution<float> _sampling_distribution;
};

}  // namespace datadog::impl
