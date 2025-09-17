// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/core.hpp"

#include <limits>

#include "core/core.hpp"

namespace datadog {

CoreConfig::CoreConfig(
    std::string_view in_client_token, std::string_view in_service,
    std::string_view in_env
)
    : client_token(in_client_token), service(in_service), env(in_env) {}

CoreConfig::~CoreConfig() = default;
CoreConfig::CoreConfig(const CoreConfig&) = default;
CoreConfig& CoreConfig::operator=(const CoreConfig&) = default;
CoreConfig::CoreConfig(CoreConfig&&) = default;
CoreConfig& CoreConfig::operator=(CoreConfig&&) = default;

CoreConfig& CoreConfig::SetInitialTrackingConsent(TrackingConsent value) {
  tracking_consent = value;
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

Core::Core(std::unique_ptr<impl::Core>&& impl, Core::PrivateCtorTag)
    : _impl(std::move(impl)) {}

Core::~Core() = default;

std::shared_ptr<Core> Core::Create(const CoreConfig& config) {
  // Validate the config: if we don't have all required parameters, return a no-op Core
  if (config.client_token.empty() || config.service.empty() || config.env.empty()) {
    return std::make_shared<Core>(nullptr, Core::PrivateCtorTag{});
  }

  // Create core subsystems using default implementations
  auto subsystems = impl::CoreSubsystems::Init(config);
  if (!subsystems) {
    // If we fail to create subsystems, return a no-op Core
    return std::make_shared<Core>(nullptr, Core::PrivateCtorTag{});
  }

  // Create the core implementation
  auto impl = std::make_unique<impl::Core>(config, std::move(*subsystems));
  if (!impl->Init()) {
    // If subsystem initialization fails, return a no-op core
    return std::make_shared<Core>(nullptr, Core::PrivateCtorTag{});
  }

  // Wrap the implementation in a C++ struct that exposes the public API for the core,
  // using the pimpl idiom, with automatic cleanup
  return std::make_shared<Core>(std::move(impl), PrivateCtorTag{});
}

void Core::SetTrackingConsent(TrackingConsent value) {
  if (_impl) {
    _impl->SetTrackingConsent(value);
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
