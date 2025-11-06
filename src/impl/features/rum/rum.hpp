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
#include "datadog/attribute.hpp"
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

  /** Handles a StartView API call, creating a new view with the given key. */
  void StartView(
      std::string_view key,
      std::string_view name = std::string_view{},
      const Attribute& attributes = Attribute()
  );

  /** Handles a StopView API call, ending the view that corresponds to the given key. */
  void StopView(std::string_view key, const Attribute& attributes = Attribute());

 private:
  RumCommandParams GetBaseCommandParams(
      const Attribute& attributes = Attribute()
  ) const;

  void Dispatch(const RumCommand& command);

  void UpdateFeatureContext();
  void UpdateApplicationSnapshot();

 private:
  // Global attributes applied to all RUM events
  ObjectAttribute _global_attributes;
  mutable std::shared_mutex _global_attributes_mutex;

  // Input dependencies passed to all child scopes by reference
  RumScopeDependencies _deps;

  // Root scope in the hierarchy that models current application state
  RumApplicationScope _application;

  // Reusable struct for storing the latest snapshot of RUM application state
  RumContext _application_snapshot;

  // HTTP request details used on upload; owned by the upload thread
  std::string _request_url;
  std::string _request_headers;
};

}  // namespace datadog::impl
