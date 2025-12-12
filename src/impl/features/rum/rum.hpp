// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>

#include "attribute/typed_attribute.hpp"
#include "core/feature.hpp"
#include "datadog/attribute.hpp"
#include "datadog/rum.hpp"
#include "diagnostics.hpp"
#include "features/rum/command.hpp"
#include "features/rum/context.hpp"
#include "features/rum/resource_types.hpp"
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
  void AddAttribute(std::string_view name, const Attribute& value);

  /** Clears a global attribute value. */
  void RemoveAttribute(std::string_view name);

  /** Handles a StopSession API call, clearing the active session. */
  void StopSession();

  /** Handles a StartView API call, creating a new view with the given key. */
  void StartView(
      std::string_view key,
      std::string_view name = std::string_view{},
      const Attribute& attributes = Attribute()
  );

  /**
   * Handles an AddViewAttribute API call, mutating the set of attributes stored for the
   * active view scope.
   */
  void AddViewAttribute(std::string_view name, const Attribute& value);

  /**
   * Handles a RemoveViewAttribute API call, mutating the set of attributes stored for
   * the active view scope.
   */
  void RemoveViewAttribute(std::string_view name);

  /** Handles a StopView API call, ending the view that corresponds to the given key. */
  void StopView(std::string_view key, const Attribute& attributes = Attribute());

  /** Handles an AddAction API call. */
  void AddAction(
      RumActionType type,
      std::string_view name,
      const Attribute& attributes = Attribute()
  );

  /**
   * Handles a StartAction API call, recording a continuous user action of the given
   * type.
   */
  void StartAction(
      RumActionType type,
      std::string_view name,
      const Attribute& attributes = Attribute()
  );

  /**
   * Handles a StopAction API call, recording the end of the currently-active continuous
   * user action, if any.
   */
  void StopAction(std::string_view new_name, const Attribute& attributes = Attribute());

  /**
   * Handles a StartResource API call, opening a new resource scope with the given key
   * in the currently-active view.
   */
  void StartResource(
      std::string_view key,
      RumRequestDetails request,
      const Attribute& attributes = Attribute()
  );

  /**
   * Handles a StopResource or StopResourceWithError API call, closing the resource
   * scope with the given key, if such a scope exists in the current view.
   */
  void StopResource(
      std::string_view key,
      RumResponseDetails response,
      std::optional<RumErrorDetails> error = std::nullopt,
      const Attribute& attributes = Attribute()
  );

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
