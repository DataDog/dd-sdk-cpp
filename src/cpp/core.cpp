// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2024-Present Datadog, Inc.

#include "datadog/core.hpp"

#include "core/core.hpp"

namespace datadog {

Core::Core(std::unique_ptr<impl::Core>&& impl, Core::PrivateCtorTag)
    : _impl(std::move(impl)) {}

Core::~Core() = default;

std::shared_ptr<Core> Core::Create(const CoreConfig& config) {
  // Create core subsystems using default implementations
  auto subsystems = impl::CoreSubsystems::Init(config);
  if (!subsystems) {
    return nullptr;
  }

  // Create the core implementation
  auto impl = std::make_unique<impl::Core>(config, std::move(*subsystems));
  if (!impl->Init()) {
    return nullptr;
  }

  // Wrap the implementation in a C++ struct that exposes the public API for the core,
  // using the pimpl idiom, with automatic cleanup
  return std::make_shared<Core>(std::move(impl), PrivateCtorTag{});
}

bool Core::Start() { return _impl->Start(); }

void Core::Stop() { _impl->Stop(); }

}  // namespace datadog
