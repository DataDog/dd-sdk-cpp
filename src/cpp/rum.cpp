// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/rum.hpp"

#include "core/core.hpp"
#include "core/feature.hpp"
#include "datadog/core.hpp"
#include "features/rum/rum.hpp"

namespace datadog {

std::shared_ptr<Rum> Rum::Register(Core& core, const RumConfig& config) {
  (void)config;  // Unused for now - this is a "do-nothing" API

  // Get essential state from the Core
  const platform::IClock& clock = core._impl->GetClock();
  std::string_view service_name = core._impl->GetServiceName();
  std::string_view application_version = core._impl->GetApplicationVersion();

  // Initialize our RUM feature implementation
  auto rum_impl = std::make_shared<impl::Rum>(clock, service_name, application_version);

  // Register the feature with the core, aborting on failure
  if (!core._impl->RegisterFeature(rum_impl)) {
    // TODO: Return a no-op interface
    return nullptr;
  }

  // Initialize and return the API object that represents our user-facing interface for
  // the RUM feature
  const std::shared_ptr<Rum> rum = std::make_shared<Rum>();
  rum->_impl = std::move(rum_impl);
  return rum;
}

}  // namespace datadog
