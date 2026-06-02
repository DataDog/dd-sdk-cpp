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
#include "datadog/impl/logging/tags.hpp"

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
   *
   * Attribute values are arbitrary JSON-encoded values that will be merged into the
   * LogEvent payload as top-level object properties.
   */
  void AddAttribute(std::string_view key, const Attribute& value);

  /**
   * Removes a logger-level attribute, if any exists with the given name.
   */
  void RemoveAttribute(std::string_view key);

  /**
   * Adds a custom tag to this logger.
   *
   * Tags are '<key>:<value>' strings that will be concatenated with a set of internal
   * tag values ('service', 'version', 'env', etc.), and included in the LogEvent
   * payload's 'ddtags' property as a comma-delimited string.
   */
  void AddTag(std::string_view tag, const DiagnosticLogger& diagnostic_logger);

  /**
   * Adds a custom tag to this logger.
   *
   * For convenience, the API allows the application to provide both pre-formatted tag
   * entries and separate (key, value) pairs. These constitute two separate code paths,
   * since implementing this function as `AddTag(key + ":" + value)` would require an
   * extra allocation.
   */
  void AddTag(
      std::string_view key,
      std::string_view value,
      const DiagnosticLogger& diagnostic_logger
  );

  /**
   * Removes any tag that was previously added to this logger with the given value.
   * Requires an exact match: e.g. `RemoveTag("foo:value1")` will remove `foo:value1`
   * but not `foo:value2`.
   *
   * Values are normalized prior to lookup: e.g. `RemoveTag("Foo:Bar")` will remove a
   * tag that was previously added as `foo:bar`
   */
  void RemoveTag(std::string_view tag);

  /**
   * Removes all tags that match the given key: e.g. `RemoveTagsWithKey("Foo")` will
   * remove any tags added as `foo`, `foo:value1`, `fOO:value2`, etc.
   */
  void RemoveTagsWithKey(std::string_view key);

  /**
   * Records a single Log call from the application, with the given log level, message
   * text, optional error details (if applicable) and set of custom log-level attribute
   * values.
   *
   * If `level` meets the configured threshold and a sampling decision passes, a
   * context-thread callback will be enqueued, causing a `LogEvent` to be generated and
   * flushed to disk in response to this call.
   *
   * `message`, `err.kind`, and `err.stack` are views of application-provided string
   * values, whose ownership and lifetime we do not control. If the remote sampling
   * decision passes, we will make copies of all relevant strings.
   *
   * Any values given in `attributes` whose names match existing logger-level or global
   * logging attributes will take precedence, shadowing those earlier values.
   */
  void Log(
      LogLevel level,
      std::string_view message,
      const LogError& err,
      const Attribute& attributes
  ) const;

 private:
  /**
   * Given the result of an attempt to add a custom tag to this logger:
   * - does nothing if the tag was accepted as-is
   * - logs a warning if the tag was accepted in modified form
   * - logs an error if the tag was rejected
   */
  static void HandleAddTagResult(
      LoggerTags::AddResult res,
      std::string_view input_key,
      const DiagnosticLogger& diagnostic_logger
  );

  /**
   * Determines whether the logger should generate a remote log event in response to a
   * new log call. Applies the configured log level threshold, making a random sampling
   * decision if needed.
   */
  bool ShouldSendLogEvent(LogLevel level) const;

  std::weak_ptr<Logging> _logging;  // Reference to Logging feature implementation
  Attribute _attributes;            // Logger-level attributes applied to all messages
  LoggerTags _tags;                 // Custom tags to be appended to 'ddtags'

  // Immutable snapshot of configuration details that affect log messages
  std::shared_ptr<const LoggerConfigDetails> _details;

  // Sampling state used in the caller thread
  LogLevel _remote_log_threshold;
  float _remote_sample_rate_unit;
  mutable std::mt19937 _sampling_rng;
  mutable std::uniform_real_distribution<float> _sampling_distribution;
};

}  // namespace datadog::impl
