// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/core.hpp"

#include "core/core.hpp"

namespace datadog {

Core::Core(std::unique_ptr<impl::Core>&& impl, Core::PrivateCtorTag)
    : _impl(std::move(impl)) {}

Core::~Core() = default;

std::shared_ptr<Core> Core::Create(const CoreConfig& config) {
  // Validate the config: if we don't have the required parameters, return a no-op Core
  if (config.client_token.empty()) {
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
