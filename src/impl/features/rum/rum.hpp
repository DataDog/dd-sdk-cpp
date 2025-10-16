// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <shared_mutex>
#include <string>
#include <string_view>

#include "attribute/typed_attribute.hpp"
#include "core/feature.hpp"
#include "datadog/rum.hpp"
#include "features/rum/command.hpp"
#include "features/rum/context.hpp"
#include "features/rum/scopes/application.hpp"
#include "platform/clock.hpp"

namespace datadog::impl {

/**
 * RUM feature implementation.
 */
class Rum final : public Feature {
 public:
  explicit Rum(const RumConfig& config, const platform::IClock& clock);

  FeatureId GetId() const override { return CreateFeatureId("RUMM"); }

  std::string_view GetName() const override { return "rum"; }

  std::optional<Report> UploadThread_PrepareReport(
      const HttpContext& context, BatchReader& reader
  ) override;

 protected:
  /** Responds to SDK start by creating an initial RUM session. */
  void Start() override;

  /** Responds to SDK stop by clearing any RUM-related global state. */
  void Stop() override;

 public:
  /** Sets an attribute value that will be included in all RUM event payloads. */
  void SetAttribute(std::string_view name, const Attribute& value);

  /** Clears a global attribute value. */
  void DeleteAttribute(std::string_view name);

  /** Handles a StopSession API call, clearing the active session. */
  void StopSession();

 private:
  RumCommandParams GetBaseCommandParams(
      const ObjectAttribute& attributes = ObjectAttribute(0)
  ) const;

  void Dispatch(const RumCommand& command);

  void UpdateFeatureContext();
  void UpdateApplicationSnapshot();

 private:
  // Injected on init
  const platform::IClock& _clock;

  // Global attributes applied to all RUM events
  ObjectAttribute _global_attributes;
  mutable std::shared_mutex _global_attributes_mutex;

  // Input dependencies passed to all child scopes by reference
  RumScopeDependencies _deps;

  // Root scope in the hierarchy that models current application state
  RumApplicationScope _application;

  RumContext _application_snapshot;
};

}  // namespace datadog::impl
