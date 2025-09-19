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

RumConfig::RumConfig(std::string_view in_application_id)
    : application_id(in_application_id) {}

RumConfig::~RumConfig() = default;
RumConfig::RumConfig(const RumConfig&) = default;
RumConfig& RumConfig::operator=(const RumConfig&) = default;
RumConfig::RumConfig(RumConfig&&) = default;
RumConfig& RumConfig::operator=(RumConfig&&) = default;

RumConfig& RumConfig::SetApplicationId(std::string_view value) {
  application_id = value;
  return *this;
}

Rum::Rum(std::shared_ptr<impl::Rum>&& impl, PrivateCtorTag) : _impl(std::move(impl)) {}

Rum::~Rum() = default;

std::shared_ptr<Rum> Rum::Register(
    const std::shared_ptr<Core>& core, const RumConfig& config
) {
  // Return a no-op Rum interface if called without a valid core
  if (!core || !core->_impl) {
    return std::make_shared<Rum>(nullptr, Rum::PrivateCtorTag{});
  }

  // If we don't have all required config values, return a no-op Rum interface
  if (config.application_id.empty()) {
    return std::make_shared<Rum>(nullptr, Rum::PrivateCtorTag{});
  }

  // Get essential state from the Core
  const platform::IClock& clock = core->_impl->GetClock();

  // Initialize our RUM feature implementation
  auto rum_impl = std::make_shared<impl::Rum>(config, clock);

  // Register the feature with the core, returning a no-op interface on failure
  if (!core->_impl->RegisterFeature(rum_impl)) {
    return std::make_shared<Rum>(nullptr, Rum::PrivateCtorTag{});
  }

  // Initialize and return the API object that represents our user-facing interface for
  // the RUM feature
  return std::make_shared<Rum>(std::move(rum_impl), Rum::PrivateCtorTag{});
}

}  // namespace datadog
