// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/logging.hpp"

#include "datadog/core.hpp"

#include "datadog/impl/core/core.hpp"
#include "datadog/impl/core/feature.hpp"
#include "datadog/impl/core/util/diagnostics.hpp"
#include "datadog/impl/logging/logger.hpp"
#include "datadog/impl/logging/logging.hpp"

namespace datadog {

LoggerConfig::LoggerConfig() = default;
LoggerConfig::~LoggerConfig() = default;
LoggerConfig::LoggerConfig(const LoggerConfig&) = default;
LoggerConfig& LoggerConfig::operator=(const LoggerConfig&) = default;
LoggerConfig::LoggerConfig(LoggerConfig&&) = default;
LoggerConfig& LoggerConfig::operator=(LoggerConfig&&) = default;

LoggerConfig& LoggerConfig::SetRemoteSampleRate(float value) {
  remote_sample_rate = value;
  return *this;
}

LoggerConfig& LoggerConfig::SetService(std::string_view value) {
  if (value.empty()) {
    service.reset();
  } else {
    service = value;
  }
  return *this;
}

LoggerConfig& LoggerConfig::SetName(std::string_view value) {
  if (value.empty()) {
    name.reset();
  } else {
    name = value;
  }
  return *this;
}

LoggerConfig& LoggerConfig::SetRemoteLogThreshold(LogLevel value) {
  remote_log_threshold = value;
  return *this;
}

LoggerConfig& LoggerConfig::SetInitialAttributeCapacity(size_t value) {
  initial_attribute_capacity = value;
  return *this;
}

LoggerConfig& LoggerConfig::SetEnrichWithRumContext(bool value) {
  enrich_with_rum_context = value;
  return *this;
}

Logger::Logger(
    std::unique_ptr<impl::Logger>&& impl,
    DiagnosticHandler diagnostic_handler,
    DiagnosticLevel diagnostic_threshold,
    PrivateCtorTag
)
    : _impl(std::move(impl)),
      _diagnostic_handler(std::move(diagnostic_handler)),
      _diagnostic_threshold(diagnostic_threshold) {}

Logger::~Logger() = default;

void Logger::AddAttribute(std::string_view name, const Attribute& value) {
  if (_impl) {
    _impl->AddAttribute(name, value);
  }
}

void Logger::RemoveAttribute(std::string_view name) {
  if (_impl) {
    _impl->RemoveAttribute(name);
  }
}

void Logger::AddTag(std::string_view tag) {
  if (_impl) {
    _impl->AddTag(
        tag, impl::DiagnosticLogger{_diagnostic_handler, _diagnostic_threshold}
    );
  }
}

void Logger::AddTag(std::string_view key, std::string_view value) {
  if (_impl) {
    _impl->AddTag(
        key, value, impl::DiagnosticLogger{_diagnostic_handler, _diagnostic_threshold}
    );
  }
}

void Logger::RemoveTag(std::string_view tag) {
  if (_impl) {
    _impl->RemoveTag(tag);
  }
}

void Logger::RemoveTagsWithKey(std::string_view key) {
  if (_impl) {
    _impl->RemoveTagsWithKey(key);
  }
}

void Logger::Log(
    LogLevel level, std::string_view message, const Attribute& attributes
) {
  if (_impl) {
    _impl->Log(level, message, attributes);
  }
}

Logging::Logging(Logging::PrivateCtorTag)
    : _impl(nullptr),
      _diagnostic_handler(nullptr),
      _diagnostic_threshold(DiagnosticLevel::Error) {}

Logging::Logging(
    std::shared_ptr<impl::Logging>&& impl,
    DiagnosticHandler diagnostic_handler,
    DiagnosticLevel diagnostic_threshold,
    Logging::PrivateCtorTag
)
    : _impl(std::move(impl)),
      _diagnostic_handler(std::move(diagnostic_handler)),
      _diagnostic_threshold(diagnostic_threshold) {
  // The C++ logging API doesn't emit any diagnostic messages, but storing these values
  // ensures that we can do so in the future without ABI changes
  (void)_diagnostic_handler;
  (void)_diagnostic_threshold;
}

Logging::~Logging() = default;

std::shared_ptr<Logging> Logging::Register(const std::shared_ptr<Core>& core) {
  // Return a no-op Logging interface if called without a valid core
  if (!core || !core->_impl) {
    return std::make_shared<Logging>(Logging::PrivateCtorTag{});
  }

  // Get essential state from the Core
  const platform::IClock& clock = core->_impl->GetClock();

  // Initialize our Logging feature implementation
  auto logging_impl = std::make_shared<impl::Logging>(clock);

  // Register the feature with the core, returning a no-op interface on failure
  if (!core->_impl->RegisterFeature(logging_impl)) {
    return std::make_shared<Logging>(Logging::PrivateCtorTag{});
  }

  // Initialize and return the API object that represents our user-facing interface for
  // the logging feature
  return std::make_shared<Logging>(
      std::move(logging_impl),
      core->_diagnostic_handler,
      core->_diagnostic_threshold,
      Logging::PrivateCtorTag{}
  );
}

void Logging::AddAttribute(std::string_view name, const Attribute& value) {
  if (_impl) {
    _impl->AddAttribute(name, value);
  }
}

void Logging::RemoveAttribute(std::string_view name) {
  if (_impl) {
    _impl->RemoveAttribute(name);
  }
}

std::shared_ptr<Logger> Logging::CreateLogger(const LoggerConfig& config) {
  if (!_impl) {
    return std::make_shared<Logger>(
        nullptr, nullptr, DiagnosticLevel::Error, Logger::PrivateCtorTag{}
    );
  }
  return std::make_shared<Logger>(
      _impl->CreateLogger(config),
      _diagnostic_handler,
      _diagnostic_threshold,
      Logger::PrivateCtorTag{}
  );
}

}  // namespace datadog
