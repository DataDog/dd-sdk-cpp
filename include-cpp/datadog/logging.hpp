// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cinttypes>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "datadog/attribute.hpp"

namespace datadog {

// Forward declarations
namespace impl {
class Logger;
class Logging;
}  // namespace impl

/**
 * Severity at which log messages may be emitted.
 */
enum class LogLevel : uint8_t {
  Debug,
  Info,
  Notice,
  Warn,
  Error,
  Critical,
};

/**
 * Configures the details of a logger upon creation.
 */
struct LoggerConfig {
  friend class Logging;
  friend class impl::Logger;

 private:
  float remote_sample_rate{100.0f};
  std::optional<std::string> service;
  std::optional<std::string> name;
  LogLevel remote_log_threshold{LogLevel::Debug};
  size_t initial_attribute_capacity{0};

 public:
  DATADOG_API LoggerConfig();
  DATADOG_API ~LoggerConfig();
  DATADOG_API LoggerConfig(const LoggerConfig&);
  DATADOG_API LoggerConfig& operator=(const LoggerConfig&);
  DATADOG_API LoggerConfig(LoggerConfig&&);
  DATADOG_API LoggerConfig& operator=(LoggerConfig&&);

  /**
   * Sets the remote sample rate to a value between 0.0 and 100.0, indicating what
   * percentage of log events should be sampled.
   */
  LoggerConfig& SetRemoteSampleRate(float value);
  /**
   * Sets the service name to be used on messages emitted by a logger. If omitted, the
   * logger will use the service name configured globally via CoreConfig.
   */
  LoggerConfig& SetService(std::string_view value);
  /**
   * Sets the name used to identify a logger in messages emitted by that logger. If
   * omitted, no 'logger.name' property will be present on log events.
   */
  LoggerConfig& SetName(std::string_view value);
  /**
   * Sets the minimum log level at which messages will be sent to intake. Only messages
   * at or above this level will be considered for sampling; all messages below that
   * level will be dropped. Defaults to LogLevel::Debug, meaning all messages will be
   * sent to intake.
   */
  LoggerConfig& SetRemoteLogThreshold(LogLevel value);
  /**
   * Sets the initial number of custom attributes for which memory will be preallocated
   * on logger creation. At the default of 0, does not reserve space for custom
   * attributes.
   *
   * Custom attributes may be freely added beyond this limit. Setting an initial
   * capacity is simply a means of optimizing memory allocations based on expected
   * usage.
   */
  LoggerConfig& SetInitialAttributeCapacity(size_t value);
};

/**
 * Interface used to emit log messages.
 */
class Logger {
  friend class Logging;

 private:
  struct PrivateCtorTag {};

 public:
  // Callers should use Logging::CreateLogger
  explicit Logger(std::unique_ptr<impl::Logger>&& impl, PrivateCtorTag);
  DATADOG_API ~Logger();

  /**
   * Adds or updates a logger-level attribute value that will be included with all
   * messages emitted by this logger. If a logger-level attribute shares its name with a
   * global attribute, the logger-level attribute will take precedence.
   */
  DATADOG_API void SetAttribute(std::string_view name, const Attribute& value);

  /**
   * Removes a logger-level attribute value, if one has been previously added with the
   * given name.
   */
  DATADOG_API void DeleteAttribute(std::string_view name);

  /**
   * Emits a log message at the given level. If attributes has type ValueType::Object,
   * each of its named values will be included in the resulting log event, taking
   * precedence over global and logger-level attributes in case of name conflict. If
   * attributes is a value of any other type, it will be ignored.
   */
  DATADOG_API void Log(
      LogLevel level, std::string_view message,
      const Attribute& attributes = Attribute()
  );

  DATADOG_API void Debug(
      std::string_view message, const Attribute& attributes = Attribute()
  ) {
    Log(LogLevel::Debug, message, attributes);
  }

  DATADOG_API void Info(
      std::string_view message, const Attribute& attributes = Attribute()
  ) {
    Log(LogLevel::Info, message, attributes);
  }

  DATADOG_API void Notice(
      std::string_view message, const Attribute& attributes = Attribute()
  ) {
    Log(LogLevel::Notice, message, attributes);
  }

  DATADOG_API void Warn(
      std::string_view message, const Attribute& attributes = Attribute()
  ) {
    Log(LogLevel::Warn, message, attributes);
  }

  DATADOG_API void Error(
      std::string_view message, const Attribute& attributes = Attribute()
  ) {
    Log(LogLevel::Error, message, attributes);
  }

  DATADOG_API void Critical(
      std::string_view message, const Attribute& attributes = Attribute()
  ) {
    Log(LogLevel::Critical, message, attributes);
  }

 private:
  // Forbid copying/moving: we use std::shared_ptr<Logger> at the API boundary
  Logger(const Logger&) = delete;
  Logger& operator=(const Logger&) = delete;
  Logger(Logger&&) = delete;
  Logger& operator=(Logger&&) = delete;

  std::unique_ptr<impl::Logger> _impl;
};

/**
 * Interface to the Datadog SDK's logging feature.
 */
class Logging {
 private:
  struct PrivateCtorTag {};

 public:
  // Callers should use Logging::Register
  explicit Logging(std::shared_ptr<impl::Logging>&& impl, PrivateCtorTag);
  DATADOG_API ~Logging();

 public:
  /**
   * Registers the logging feature with the core of the Datadog SDK.
   */
  DATADOG_API static std::shared_ptr<Logging> Register(
      const std::shared_ptr<class Core>& core
  );

  /**
   * Adds or updates a global attribute value that will be included with all log
   * messages emitted by all loggers.
   */
  DATADOG_API void SetAttribute(std::string_view name, const Attribute& value);

  /**
   * Removes a global attribute value, if one has been previously added with the given
   * name.
   */
  DATADOG_API void DeleteAttribute(std::string_view name);

  /**
   * Creates and returns a new logger with the given configuration.
   */
  DATADOG_API std::shared_ptr<Logger> CreateLogger(
      const LoggerConfig& config = LoggerConfig()
  );

 private:
  // Forbid copying/moving: we use std::shared_ptr<Logging> at the API boundary
  Logging(const Logging&) = delete;
  Logging& operator=(const Logging&) = delete;
  Logging(Logging&&) = delete;
  Logging& operator=(Logging&&) = delete;

  std::shared_ptr<impl::Logging> _impl;
};

}  // namespace datadog
