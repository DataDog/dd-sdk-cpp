// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/features/rum/rum.hpp"

#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <string_view>

#include "datadog/impl/core/writer.hpp"

namespace datadog::impl {
Rum::Rum(const RumConfig& config, const platform::IClock& clock)
    : _global_attributes(8),
      _deps(config, clock),
      _application(_deps),
      _application_snapshot() {}

std::optional<Report> Rum::UploadThread_PrepareReport(
    const HttpContext& context, BatchReader& reader
) {
  // This preliminary implementation just streams all RUM events directly, a la Logging

  // TODO(RUM-12546): Implement filtering/deduplication of view events, unless we
  // implement `view_update` events and find that make filtering unnecessary

  // TODO(RUM-12242): If necessary to prevent the creation of microsessions on the
  // backend, filter out events for synthetic views created on launch

  // Request URL
  static const std::string_view request_path = "/api/v2/rum";
  static const bool with_ddsource = true;

  // Request headers
  static const std::string_view content_type = "application/json";

  // Build URL and headers once, on the first upload (HTTP context is immutable)
  if (_request_url.empty()) {
    context.BuildRequestURL(request_path, with_ddsource, _request_url);
    context.BuildRequestHeaders(content_type, "", _request_headers);
  }

  // Each event in the batch is a JSON object: initialize a writer that will concatenate
  // each of those objects into a JSON array
  return Report{_request_url, _request_headers, TLVBatchWriter{reader}};
}

void Rum::Start() {
  // Inject a reference to our FeatureScope interface into the RumScopeDependencies that
  // will be provided to all scopes, so they can generate events etc.
  if (_scope) {
    _deps.OnStart(*_scope);
  } else {
    DATADOG_ASSERT(false, "Rum has no valid FeatureScope on Start");
  }

  // Dispatch an SDKInit command, which should kick off initialization of our first
  // session when handled by the application scope
  Dispatch(RumCommand::SDKInit(GetBaseCommandParams()));
};

void Rum::Stop() {
  // On Core shutdown, ensure that any RUM context is purged, so that if the SDK is
  // restarted it won't inherit old state
  if (_scope) {
    _scope->UpdateContext([](CoreContext& ctx) { ctx.rum.reset(); });
  }

  // Fully reinitialize RUM application state to clear all sessions/views/etc.
  _application = RumApplicationScope(_deps);

  // Clear the FeatureScope reference in RumScopeDependencies: scopes should no longer
  // generate events
  _deps.OnStop();
}

void Rum::AddAttribute(std::string_view name, const Attribute& value) {
  std::unique_lock exclusive_write_lock(_global_attributes_mutex);
  _global_attributes.attribute.SetObjectProperty(name, value);
}

void Rum::RemoveAttribute(std::string_view name) {
  std::unique_lock exclusive_write_lock(_global_attributes_mutex);
  _global_attributes.attribute.DeleteObjectProperty(name);
}

void Rum::StopSession() {
  // Dispatch a StopSession command, which the session scope should handle
  Dispatch(RumCommand::StopSession(GetBaseCommandParams()));
}

void Rum::StartView(
    std::string_view key, std::string_view name, const Attribute& attributes
) {
  // Dispatch a StartView command to be handled by the active session
  Dispatch(RumCommand::StartView(GetBaseCommandParams(attributes), key, name));
}

void Rum::AddViewAttribute(std::string_view name, const Attribute& value) {
  // TODO(RUM-11363): Log a warning if there's no active view to receive the command?
  Dispatch(RumCommand::AddViewAttribute(GetBaseCommandParams(), name, value));
}

void Rum::RemoveViewAttribute(std::string_view name) {
  Dispatch(RumCommand::RemoveViewAttribute(GetBaseCommandParams(), name));
}

void Rum::StopView(std::string_view key, const Attribute& attributes) {
  // Dispatch a StopView command
  Dispatch(RumCommand::StopView(GetBaseCommandParams(attributes), key));
}

void Rum::AddAction(
    RumActionType type, std::string_view name, const Attribute& attributes
) {
  Dispatch(RumCommand::AddAction(GetBaseCommandParams(attributes), type, name));
}

void Rum::StartAction(
    RumActionType type, std::string_view name, const Attribute& attributes
) {
  Dispatch(RumCommand::StartAction(GetBaseCommandParams(attributes), type, name));
}

void Rum::StopAction(std::string_view new_name, const Attribute& attributes) {
  Dispatch(RumCommand::StopAction(GetBaseCommandParams(attributes), new_name));
}

void Rum::StartResource(
    std::string_view key, RumRequestDetails request, const Attribute& attributes
) {
  Dispatch(RumCommand::StartResource(GetBaseCommandParams(attributes), key, request));
}

void Rum::StopResource(
    std::string_view key,
    RumResponseDetails response,
    const std::optional<RumErrorDetails>& error,
    const Attribute& attributes
) {
  Dispatch(
      RumCommand::StopResource(GetBaseCommandParams(attributes), key, response, error)
  );
}

void Rum::AddError(
    RumErrorSource source, const RumErrorDetails& error, const Attribute& attributes
) {
  Dispatch(RumCommand::AddError(GetBaseCommandParams(attributes), source, error));
}

void Rum::StartFeatureOperation(
    std::string_view name,
    std::optional<std::string_view> operation_key,
    const UUID& vital_id,
    const Attribute& attributes
) {
  Dispatch(RumCommand::StartFeatureOperation(
      GetBaseCommandParams(attributes), name, operation_key, vital_id
  ));
}

void Rum::StopFeatureOperation(
    std::string_view name,
    std::optional<std::string_view> operation_key,
    const UUID& vital_id,
    std::optional<RumFeatureOperationFailureReason> failure_reason,
    const Attribute& attributes
) {
  Dispatch(RumCommand::StopFeatureOperation(
      GetBaseCommandParams(attributes), name, operation_key, vital_id, failure_reason
  ));
}

RumCommandParams Rum::GetBaseCommandParams(const Attribute& attributes) const {
  // Create a shallow copy of the global attributes
  std::shared_lock read_only_lock(_global_attributes_mutex);
  Attribute global_attributes = _global_attributes.attribute;
  read_only_lock.unlock();

  // Read the system clock for our issued_at timestamp
  auto issued_at = _deps.clock.Now();

  return RumCommandParams(issued_at, global_attributes, attributes);
}

void Rum::Dispatch(const RumCommand& command) {
  // Refrain from dispatching any commands if we don't have a valid FeatureScope: this
  // means that the SDK has not yet started or has previously shut down
  if (!_scope) {
    return;
  }

  // Let the root RumApplicationScope handle the command: each scope will propagate
  // commands to child scope(s) at their discretion
  _application.Process(command);

  // After every command, update our RumFeatureContext, which makes current RUM state
  // available to other features within the SDK
  UpdateFeatureContext();
}

void Rum::UpdateFeatureContext() {
  // Build a RumContext value (_application_snapshot) that summarizes the current state
  // of our internal scope tree
  UpdateApplicationSnapshot();

  // Write the relevant values to the global RumFeatureContext, so that other features
  // can enrich their events with the latest RUM context
  if (_scope) {
    const RumFeatureContext rum_ctx = _application_snapshot.ToFeatureContext();
    _scope->UpdateContext([rum_ctx](CoreContext& ctx) { ctx.rum = rum_ctx; });
  }
}

void Rum::UpdateApplicationSnapshot() {
  // Reset our RumContext value to zero, without releasing string buffers etc.
  _application_snapshot.Reset();

  // If we don't have an active session, let the application scope populate our context
  auto session_opt = _application.GetActiveSession();
  if (!session_opt) {
    _application.PopulateContext(_application_snapshot);
    return;
  }

  // If we have a session but no views, populate from session scope
  const RumSessionScope& session = *session_opt;
  auto view_opt = session.GetActiveView();
  if (!view_opt) {
    session.PopulateContext(_application_snapshot);
    return;
  }

  // If our active session has an active view, but that view has no actions, populate
  // from view scope
  const RumViewScope& view = *view_opt;
  auto action_opt = view.GetActiveAction();
  if (!action_opt) {
    view.PopulateContext(_application_snapshot);
    return;
  }

  // If we have an active session, view, and action, populate from action scope
  const RumActionScope& action = *action_opt;
  action.PopulateContext(_application_snapshot);
}

}  // namespace datadog::impl
