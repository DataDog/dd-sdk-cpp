#include "datadog/logging.hpp"

#include "core/core.hpp"
#include "core/feature.hpp"
#include "datadog/core.hpp"
#include "features/logging/logger.hpp"
#include "features/logging/logging.hpp"

namespace datadog {

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

void Logger::SetAttribute(std::string_view name, const Attribute& value) {
  _impl->SetAttribute(name, value);
}

void Logger::DeleteAttribute(std::string_view name) { _impl->DeleteAttribute(name); }

void Logger::Log(
    LogLevel level, std::string_view message, const Attribute& attributes
) {
  _impl->EmitLogEvent(level, message, attributes);
}

std::shared_ptr<Logging> Logging::Register(Core& core) {
  // Get essential state from the Core
  const platform::IClock& clock = core._impl->GetClock();
  std::string_view service_name = core._impl->GetServiceName();
  std::string_view application_version = core._impl->GetApplicationVersion();

  // Initialize our Logging feature implementation
  auto logging_impl =
      std::make_shared<impl::Logging>(clock, service_name, application_version);

  // Register the feature with the core, aborting on failure
  if (!core._impl->RegisterFeature(logging_impl)) {
    // TODO: Return a no-op interface
    return nullptr;
  }

  // Initialize and return the API object that represents our user-facing interface for
  // the logging feature
  const std::shared_ptr<Logging> logging = std::make_shared<Logging>();
  logging->_impl = std::move(logging_impl);
  return logging;
}

void Logging::SetAttribute(std::string_view name, const Attribute& value) {
  _impl->SetAttribute(name, value);
}

void Logging::DeleteAttribute(std::string_view name) { _impl->DeleteAttribute(name); }

std::shared_ptr<Logger> Logging::CreateLogger(const LoggerConfig& config) {
  auto logger = std::make_shared<Logger>();
  logger->_impl = _impl->CreateLogger(config);
  return logger;
}

}  // namespace datadog
