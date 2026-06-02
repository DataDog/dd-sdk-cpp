// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/core.hpp"

#include <iostream>
#include <limits>

#include "datadog/impl/core/core.hpp"
#include "datadog/impl/core/util/diagnostics.hpp"

namespace datadog {

void StderrDiagnosticHandler(const DiagnosticMessage& message) {
  static const char* level_names[] = {"DEBUG", "STATUS", "WARNING", "ERROR"};
  const size_t i = static_cast<size_t>(message.level);
  const char* level_name = i < std::size(level_names) ? level_names[i] : "";  // NOLINT
  std::cerr << "[DATADOG " << level_name << "] " << message.text << "\n";
}

CoreConfig::CoreConfig(
    std::string_view in_client_token,
    std::string_view in_service,
    std::string_view in_env
)
    : client_token(in_client_token), service(in_service), env(in_env) {}

CoreConfig::~CoreConfig() = default;
CoreConfig::CoreConfig(const CoreConfig&) = default;
CoreConfig& CoreConfig::operator=(const CoreConfig&) = default;
CoreConfig::CoreConfig(CoreConfig&&) = default;
CoreConfig& CoreConfig::operator=(CoreConfig&&) = default;

CoreConfig& CoreConfig::SetDiagnosticHandler(DiagnosticHandler value) {
  diagnostic_handler = std::move(value);
  return *this;
}

CoreConfig& CoreConfig::SetDiagnosticThreshold(DiagnosticLevel value) {
  diagnostic_threshold = value;
  return *this;
}

CoreConfig& CoreConfig::SetApplicationStoragePath(std::string_view value) {
  application_storage_path = value;
  return *this;
}

CoreConfig& CoreConfig::SetSite(Site value) {
  site = value;
  return *this;
}

CoreConfig& CoreConfig::SetClientToken(std::string_view value) {
  client_token = value;
  return *this;
}

CoreConfig& CoreConfig::SetService(std::string_view value) {
  service = value;
  return *this;
}

CoreConfig& CoreConfig::SetEnv(std::string_view value) {
  env = value;
  return *this;
}

CoreConfig& CoreConfig::SetApplicationVersion(std::string_view value) {
  application_version = value;
  return *this;
}

CoreConfig& CoreConfig::SetVariant(std::string_view value) {
  variant = value;
  return *this;
}

CoreConfig& CoreConfig::SetBatchSize(BatchSize value) {
  batch_size = value;
  return *this;
}

CoreConfig& CoreConfig::SetUploadFrequency(UploadFrequency value) {
  upload_frequency = value;
  return *this;
}

CoreConfig& CoreConfig::SetBatchProcessingLevel(BatchProcessingLevel value) {
  batch_processing_level = value;
  return *this;
}

CoreConfig& CoreConfig::Internal_FlushHttpRequestsOnStop() {
  const size_t value = std::numeric_limits<size_t>::max();
  internal_options.num_http_requests_per_feature_to_flush_on_stop = value;
  return *this;
}

CoreConfig& CoreConfig::Internal_UseCustomEndpoint(std::string_view value) {
  internal_options.custom_endpoint_url = value;
  return *this;
}

CoreConfig& CoreConfig::Internal_SetSource(std::string_view value) {
  internal_options.source = value;
  return *this;
}

CoreConfig& CoreConfig::Internal_SetSdkVersion(std::string_view value) {
  internal_options.sdk_version = value;
  return *this;
}

Core::Core(Core::PrivateCtorTag)
    : _impl(nullptr),
      _diagnostic_handler(nullptr),
      _diagnostic_threshold(DiagnosticLevel::Error) {}

Core::Core(
    std::unique_ptr<impl::Core>&& impl,
    DiagnosticHandler diagnostic_handler,
    DiagnosticLevel diagnostic_threshold,
    PrivateCtorTag
)
    : _impl(std::move(impl)),
      _diagnostic_handler(std::move(diagnostic_handler)),
      _diagnostic_threshold(diagnostic_threshold) {}

Core::~Core() = default;

std::shared_ptr<Core> Core::Create(
    const CoreConfig& config, TrackingConsent tracking_consent
) {
  // Prepare a diagnostic logger that will allow us to emit errors for invalid API usage
  const impl::DiagnosticLogger diagnostic_logger{
      config.diagnostic_handler, config.diagnostic_threshold
  };

  // Validate the config: if we don't have all required parameters, return a no-op Core
  if (config.client_token.empty()) {
    diagnostic_logger.Error(
        "SDK initialization failed: application must supply a non-empty 'client_token' "
        "value in CoreConfig"
    );
    return std::make_shared<Core>(Core::PrivateCtorTag{});
  }
  if (config.service.empty()) {
    diagnostic_logger.Error(
        "SDK initialization failed: application must supply a non-empty 'service' "
        "value in CoreConfig"
    );
    return std::make_shared<Core>(Core::PrivateCtorTag{});
  }
  if (config.env.empty()) {
    diagnostic_logger.Error(
        "SDK initialization failed: application must supply a non-empty 'env' value in "
        "CoreConfig"
    );
    return std::make_shared<Core>(Core::PrivateCtorTag{});
  }

  // Create core subsystems using default implementations
  auto subsystems_result = impl::CoreSubsystems::Init(config);
  if (!subsystems_result) {
    // If we fail to initialize subsystems, log an error and return a no-op Core
    auto err = subsystems_result.error().AddPrefix("SDK initialization failed");
    diagnostic_logger.Error(err.Format().c_str());
    return std::make_shared<Core>(Core::PrivateCtorTag{});
  }
  impl::CoreSubsystems subsystems = std::move(*subsystems_result);

  // Create the core implementation
  auto impl =
      std::make_unique<impl::Core>(config, tracking_consent, std::move(subsystems));
  if (!impl->Init()) {
    // If subsystem initialization fails, return a no-op core
    return std::make_shared<Core>(Core::PrivateCtorTag{});
  }

  // Wrap the implementation in a C++ struct that exposes the public API for the core,
  // using the pimpl idiom, with automatic cleanup
  return std::make_shared<Core>(
      std::move(impl),
      config.diagnostic_handler,
      config.diagnostic_threshold,
      PrivateCtorTag{}
  );
}

void Core::SetTrackingConsent(TrackingConsent value) {
  if (_impl) {
    _impl->SetTrackingConsent(value);
  }
}

void Core::SetUserInfo(
    std::string_view id,
    std::string_view name,
    std::string_view email,
    const Attribute& extra_info
) {
  if (_impl) {
    _impl->SetUserInfo(id, name, email, extra_info);
  }
}

void Core::AddUserExtraInfo(const Attribute& extra_info) {
  if (_impl) {
    _impl->AddUserExtraInfo(extra_info);
  }
}

void Core::ClearUserInfo() {
  if (_impl) {
    _impl->ClearUserInfo();
  }
}

void Core::SetAccountInfo(
    std::string_view id, std::string_view name, const Attribute& extra_info
) {
  if (_impl) {
    _impl->SetAccountInfo(id, name, extra_info);
  }
}

void Core::AddAccountExtraInfo(const Attribute& extra_info) {
  if (_impl) {
    _impl->AddAccountExtraInfo(extra_info);
  }
}

void Core::ClearAccountInfo() {
  if (_impl) {
    _impl->ClearAccountInfo();
  }
}

bool Core::Start() {
  if (_impl) {
    return _impl->Start();
  }
  return false;
}

void Core::Stop() {
  if (_impl) {
    _impl->Stop();
  }
}

}  // namespace datadog
