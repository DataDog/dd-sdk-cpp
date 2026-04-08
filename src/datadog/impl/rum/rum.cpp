// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/rum/rum.hpp"

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
  // Fully reinitialize RUM application state to clear all sessions/views/etc. from
  // previous runs
  _application = RumApplicationScope(_deps);

  // Dispatch SDKInit to start first session
  DispatchAsync(RumCommand::SDKInit(GetBaseCommandParams()));
};

void Rum::Stop() {
  // Note: _application will be destroyed when Rum is destroyed (after Core joins
  // the context thread). In-flight lambdas can safely access _application as long as
  // weak_ptr.lock() succeeds. CoreContext is reset by Core::Start() at the top of each
  // new run, so no context mutation is needed here.
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
  DispatchAsync(RumCommand::StopSession(GetBaseCommandParams()));
}

void Rum::StartView(
    std::string_view key, std::string_view name, const Attribute& attributes
) {
  // Dispatch a StartView command to be handled by the active session
  DispatchAsync(RumCommand::StartView(GetBaseCommandParams(attributes), key, name));
}

void Rum::AddViewAttribute(std::string_view name, const Attribute& value) {
  // TODO(RUM-11363): Log a warning if there's no active view to receive the command?
  DispatchAsync(RumCommand::AddViewAttribute(GetBaseCommandParams(), name, value));
}

void Rum::RemoveViewAttribute(std::string_view name) {
  DispatchAsync(RumCommand::RemoveViewAttribute(GetBaseCommandParams(), name));
}

void Rum::StopView(std::string_view key, const Attribute& attributes) {
  // Dispatch a StopView command
  DispatchAsync(RumCommand::StopView(GetBaseCommandParams(attributes), key));
}

void Rum::AddAction(
    RumActionType type, std::string_view name, const Attribute& attributes
) {
  DispatchAsync(RumCommand::AddAction(GetBaseCommandParams(attributes), type, name));
}

void Rum::StartAction(
    RumActionType type, std::string_view name, const Attribute& attributes
) {
  DispatchAsync(RumCommand::StartAction(GetBaseCommandParams(attributes), type, name));
}

void Rum::StopAction(std::string_view new_name, const Attribute& attributes) {
  DispatchAsync(RumCommand::StopAction(GetBaseCommandParams(attributes), new_name));
}

void Rum::StartResource(
    std::string_view key, const RumRequestDetails& request, const Attribute& attributes
) {
  DispatchAsync(
      RumCommand::StartResource(GetBaseCommandParams(attributes), key, request)
  );
}

void Rum::StopResource(
    std::string_view key,
    RumResponseDetails response,
    const std::optional<RumErrorDetails>& error,
    const Attribute& attributes
) {
  DispatchAsync(
      RumCommand::StopResource(GetBaseCommandParams(attributes), key, response, error)
  );
}

void Rum::AddError(
    RumErrorSource source, const RumErrorDetails& error, const Attribute& attributes
) {
  DispatchAsync(RumCommand::AddError(GetBaseCommandParams(attributes), source, error));
}

void Rum::StartFeatureOperation(
    std::string_view name,
    std::optional<std::string_view> operation_key,
    const Attribute& attributes
) {
  DispatchAsync(
      RumCommand::StartFeatureOperation(
          GetBaseCommandParams(attributes), name, operation_key
      )
  );
}

void Rum::StopFeatureOperation(
    std::string_view name,
    std::optional<std::string_view> operation_key,
    std::optional<RumOperationFailureReason> failure_reason,
    const Attribute& attributes
) {
  DispatchAsync(
      RumCommand::StopFeatureOperation(
          GetBaseCommandParams(attributes), name, operation_key, failure_reason
      )
  );
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

void Rum::DispatchAsync(const RumCommand& command) {
  // If our _scope is no longer valid, the SDK has been stopped prior to this call
  if (!_scope) {
    return;
  }
  FeatureScope& scope = *_scope;

  // Capture weak_ptr to self for fail-safe shutdown detection
  auto weak_rum = std::weak_ptr<Rum>(std::static_pointer_cast<Rum>(shared_from_this()));

  // Enqueue a function to run on the context thread, capturing the command being
  // dispatched: when this function is executed, the context thread will process the
  // command, updating internal RUM state and potentially producing events
  scope.ExecuteOnContextThread(
      [weak_rum, cmd = command](const CoreContext& context, const EventWriter& writer) {
        // Single-level check: Is Rum object still alive?
        auto rum = weak_rum.lock();
        if (!rum) {
          // Rum destroyed during shutdown, exit gracefully
          return;
        }

        // Safe to proceed - processing uses only deps, context, writer, and
        // _application (all valid as long as Rum is alive)
        rum->_application.Process(cmd, context, writer);

        // After every command, build a RumContext value (in _application_snapshot) that
        // describes the state of our internal scope tree
        rum->UpdateApplicationSnapshot();
      }
  );

  // Enqueue a context-mutation function that will run immediately following the
  // processing of the command
  scope.UpdateContext([weak_rum](CoreContext& ctx) {
    if (auto rum = weak_rum.lock()) {
      // _application_snapshot has been updated with the result of processing our
      // command; write the relevant UUIDs to the global RumFeatureContext, so that
      // other features can enrich their events with RUM data
      ctx.rum = rum->_application_snapshot.ToFeatureContext();
    }
  });
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
