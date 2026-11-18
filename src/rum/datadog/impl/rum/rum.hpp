// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <atomic>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>

#include "datadog/attribute.hpp"
#include "datadog/rum.hpp"

#include "datadog/impl/core/feature.hpp"
#include "datadog/impl/core/platform/clock.hpp"
#include "datadog/impl/rum/command.hpp"
#include "datadog/impl/rum/context.hpp"
#include "datadog/impl/rum/resource_types.hpp"
#include "datadog/impl/rum/scopes/application.hpp"
#include "datadog/impl/types/diagnostics.hpp"

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
      BatchReader& reader, HttpRequestBuilder& builder
  ) override;

  std::optional<std::function<void(const FeatureMessage&)>>
  MakeMessageHandler() override;

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
      const RumRequestDetails& request,
      const Attribute& attributes = Attribute()
  );

  /**
   * Handles a StopResource or StopResourceWithError API call, closing the resource
   * scope with the given key, if such a scope exists in the current view.
   */
  void StopResource(
      std::string_view key,
      RumResponseDetails response,
      const std::optional<RumErrorDetails>& error = std::nullopt,
      const Attribute& attributes = Attribute()
  );

  /**
   * Handles an AddError API call, causing an error to be reported in the context of the
   * current view.
   */
  void AddError(
      RumErrorSource source,
      const RumErrorDetails& error,
      const Attribute& attributes = Attribute()
  );

  /**
   * Handles an AddLongTask API call, causing a long_task to be reported in the context
   * of the current view.
   */
  void AddLongTask(Duration duration, const Attribute& attributes = Attribute());

  /**
   * Handles a StartOperation API call, recording the start of a user-facing
   * operation.
   */
  void StartOperation(
      std::string_view name,
      std::optional<std::string_view> operation_key,
      const Attribute& attributes = Attribute()
  );

  /**
   * Handles a StopOperation API call (succeed or fail), recording the conclusion
   * of a user-facing operation.
   */
  void StopOperation(
      std::string_view name,
      std::optional<std::string_view> operation_key,
      std::optional<RumOperationFailureReason> failure_reason,
      const Attribute& attributes = Attribute()
  );

  /**
   * Handles a ReportAppDisplayInitialized API call. Dispatches a command that causes
   * the session scope to emit a TTID app-launch vital event. Subsequent calls are
   * silently dropped (with a warning) until the SDK is stopped and restarted.
   */
  void ReportAppDisplayInitialized();

  /**
   * Handles a ReportAppFullyDisplayed API call. Dispatches a command that causes
   * the session scope to emit a TTFD app-launch vital event. Subsequent calls are
   * silently dropped (with a warning) until the SDK is stopped and restarted.
   */
  void ReportAppFullyDisplayed();

 private:
  RumCommandParams GetBaseCommandParams(
      const Attribute& attributes = Attribute()
  ) const;

  void DispatchAsync(const RumCommand& command);

  void UpdateApplicationSnapshot();

  /**
   * Examines the current state of `_application` and `_application_snapshot` and
   * publishes any `FeatureMessage` values that reflect state changes since the last
   * call. Must be called on the context thread after `UpdateApplicationSnapshot()`.
   */
  void BroadcastStateChanges(const MessagePublisher& publisher);

 private:
  // Global attributes applied to all RUM events
  Attribute _global_attributes;
  mutable std::shared_mutex _global_attributes_mutex;

  // Input dependencies passed to all child scopes by reference
  RumScopeDependencies _deps;

  // Root scope in the hierarchy that models current application state
  RumApplicationScope _application;

  // Reusable struct for storing the latest snapshot of RUM application state
  RumContext _application_snapshot;

  // Last-broadcast values used to detect changes in BroadcastStateChanges; only
  // accessed on the context thread, so no additional locking is needed
  std::optional<RumSessionState> _last_broadcast_session_state;
  UUID _last_broadcast_view_id{UUID::Zero};

  // Guards against duplicate ReportAppDisplayInitialized() calls; cleared on Stop()
  std::atomic<bool> _ttid_reported{false};

  // Guards against duplicate ReportAppFullyDisplayed() calls; cleared on Stop()
  std::atomic<bool> _ttfd_reported{false};

  // HTTP request details used on upload; owned by the upload thread
  std::string _request_url;
  std::string _request_headers;
};

}  // namespace datadog::impl
