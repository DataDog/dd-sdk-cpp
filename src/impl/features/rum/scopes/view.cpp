// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "features/rum/scopes/view.hpp"

#include "core/feature_scope.hpp"
#include "datadog/timestamp.hpp"
#include "features/rum/context.hpp"
#include "features/rum/scopes/session.hpp"

namespace datadog::impl {

RumViewScope::RumViewScope(
    const RumScopeDependencies& deps,
    RumSessionScope& parent,
    bool is_initial_view,
    const UUID& view_id,
    std::string_view key,
    std::string_view name,
    Timestamp start_time
)
    : _deps(deps),
      _parent(parent),
      _is_initial_view(is_initial_view),
      _view_id(view_id),
      _key(key),
      _name(name),
      _started_at(start_time),
      _attributes(0) {}

void RumViewScope::PopulateContext(struct RumContext& out_context) const {
  // Call the parent's PopulateContext function to set application and session details
  const RumSessionScope& parent = _parent;
  parent.PopulateContext(out_context);

  // Set view-specific parameters: assume that if we're being asked to populate a
  // RumContext, we are in fact the active view
  out_context.active_view_id = _view_id;
  out_context.active_view_key = _key;
  out_context.active_view_name = _name;
}

RumScopeResult RumViewScope::Process(const RumCommand& command) {
  // TODO(RUM-11369): Propagate to child action scopes

  // Process the command, updating our internal state as needed
  ViewEventType event_type = HandleCommand(command);

  // TODO(RUM-12202): If processing a resource command, propagate that command _only_ to
  // the child resource scope that matches the target resource key, if any

  // If this is the very first view in the application and we've not yet sent a view
  // event describing it, override the HandleCommand result and send a full event ASAP
  if (_is_initial_view && _num_view_events_sent == 0) {
    event_type = ViewEventType::Full;
  }

  // Generate a 'view' event if our state has meaningfully changed since the last event
  // we sent
  if (event_type != ViewEventType::None) {
    SendViewEvent();
  }

  // If the result of this command is that we're no longer the active view and we no
  // longer have any child resources in flight, we can be closed
  const bool has_pending_resources = false;  // TODO(RUM-12202): Initialize
  if (!_is_active && !has_pending_resources) {
    return RumScopeResult::Close;
  }

  // Otherwise, keep the scope open, regardless of whether it's active
  return RumScopeResult::RemainOpen;
}

RumViewScope::ViewEventType RumViewScope::HandleCommand(const RumCommand& command) {
  // On `StopSession`, any active views should should be implicitly stopped
  if (command.Is<RumStopSessionPayload>()) {
    // If already inactive, we can ignore StopSession
    if (!_is_active) {
      return ViewEventType::None;
    }

    // TODO(RUM-12202): If the view has pending resources on StopSession, they will
    // never receive StopResource, so there may not be any sense in keeping the session
    // scope open (while inactive) in this case

    // If this view is still active, mark it inactive and ensure that it sends a final
    // event
    _is_active = false;
    return ViewEventType::Full;
  }

  // TODO(RUM-12243): Handle ApplicationStart by assuming that we're the
  // ApplicationLaunch view and performing required initialization: namely, generating
  // an event for a custom 'application_start' Action; iff we can determine that the
  // application launch was user-initiated and coincided with SDK init

  // On `StartView`, the target view should be initialized and all other views
  // (including existing views with the same key) should be implicitly stopped
  if (command.Is<RumStartViewPayload>()) {
    // If already inactive, we can ignore StartView
    if (!_is_active) {
      return ViewEventType::None;
    }

    // If this is the first StartView command we've ever seen, and it matches our view
    // key, then _we_ are the newly-created view and we should update our state
    const auto& payload = command.As<RumStartViewPayload>();
    const bool is_new_view_scope = payload.key == _key && !_has_processed_start_view;
    if (is_new_view_scope) {
      // TODO(RUM-12322): Merge in view attributes from command

      // Ensure that future StartView calls with the same key will close this scope
      _has_processed_start_view = true;

      // Ensure that we send an initial view event
      return ViewEventType::Full;
    }

    // Otherwise, the StartView command was issued for a different view scope, and this
    // scope should be flagged inactive and should send its final event
    _is_active = false;
    return ViewEventType::Full;
  }

  // On `StopView`, any still-active views matching the target view key should become
  // inactive
  if (command.Is<RumStopViewPayload>()) {
    // If already inactive, we can ignore StopView
    if (!_is_active) {
      return ViewEventType::None;
    }

    // If the command targets a different view key, we can ignore StopView
    const auto& payload = command.As<RumStopViewPayload>();
    if (payload.key != _key) {
      return ViewEventType::None;
    }

    // Otherwise, this view should become inactive and should send a final event
    _is_active = false;
    return ViewEventType::Full;
  }

  // TODO(RUM-11369): Handle StartAction and AddAction (while _is_active) by opening a
  // child action scope and adding it to our _action_scopes array

  // TODO(RUM-12202): Handle StartResource (while _is_active) by opening a child
  // resource scope and inserting it into our _resource_scopes lookup by key

  return ViewEventType::None;
}

void RumViewScope::SendViewEvent() {
  // Resolve references needed to populate required event data
  const RumScopeDependencies& deps = _deps;
  const RumSessionScope& session = _parent;

  // Read system time for event timestamp, and compute time since view start
  const Timestamp now = deps.clock.Now();
  const Duration time_spent = now - _started_at;
  const uint64_t time_spent_ns = time_spent.count();

  // Get sums of child scope occurrences within the lifetime of this view
  const uint64_t action_count = 0;    // TODO(RUM-11369)
  const uint64_t error_count = 0;     // TODO(RUM-12201)
  const uint64_t resource_count = 0;  // TODO(RUM-12202)

  // Construct an event value on the stack with the minimal set of required properties
  RumViewEvent ev(
      now,
      deps.application_id,
      session.GetSessionID(),
      RumSessionType::User,
      _view_id,
      _key,
      time_spent_ns,
      action_count,
      error_count,
      resource_count,
      _num_view_events_sent++
  );

  // Set essential view properties
  ev.view.is_active.value = _is_active;
  if (!_name.empty()) {
    ev.view.name.value = _name;
  }

  // Serialize the event to JSON in a shared buffer, then copy that raw event payload
  // onto the storage thread
  deps.ProduceEvent(ev);
}

}  // namespace datadog::impl
