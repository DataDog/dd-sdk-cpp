// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "features/rum/scopes/view.hpp"

#include "core/feature_scope.hpp"
#include "core/feature_types/rum.hpp"
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
      _view_attributes(16),
      _global_and_view_attributes(32) {}

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
  // If we have a child action scope, allow it to process this command, and clear it if
  // the scope is closed as a result
  bool has_incremented_action_count = false;
  if (_active_action_scope) {
    const RumScopeResult action_result = _active_action_scope->Process(command);
    if (action_result == RumScopeResult::Close) {
      if (_active_action_scope->HasSentActionEvent()) {
        _num_actions_completed++;
        has_incremented_action_count = true;
      }
      _active_action_scope.reset();
    }
  }

  // Process the command, updating our internal state as needed
  ViewEventType event_type = HandleCommand(command);

  // TODO(RUM-12202): If processing a resource command, propagate that command _only_ to
  // the child resource scope that matches the target resource key, if any

  // If this is the very first view in the application and we've not yet sent a view
  // event describing it, override the HandleCommand result and send a full event ASAP
  if (_is_initial_view && _num_view_events_sent == 0) {
    event_type = ViewEventType::Full;
  }

  // Similarly, if we just closed an action scope that reported an event, trigger
  // another view update so we can report our up-to-date action count
  if (has_incremented_action_count) {
    event_type = ViewEventType::Full;
  }

  // Generate a 'view' event if our state has meaningfully changed since the last event
  // we sent
  if (event_type != ViewEventType::None) {
    SendViewEvent(command);
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
    return HandleStopSession(command.base);
  }

  // TODO(RUM-12243): Handle ApplicationStart by assuming that we're the
  // ApplicationLaunch view and performing required initialization: namely, generating
  // an event for a custom 'application_start' Action; iff we can determine that the
  // application launch was user-initiated and coincided with SDK init

  // On `StartView`, the target view should be initialized and all other views
  // (including existing views with the same key) should be implicitly stopped
  if (command.Is<RumStartViewPayload>()) {
    return HandleStartView(command.base, command.As<RumStartViewPayload>());
  }

  // On `StopView`, any still-active views matching the target view key should become
  // inactive
  if (command.Is<RumStopViewPayload>()) {
    return HandleStopView(command.base, command.As<RumStopViewPayload>());
  }

  // On `AddViewAttribute`, the given attribute value should be added or updated on the
  // active view
  if (command.Is<RumAddViewAttributePayload>()) {
    return HandleAddViewAttribute(
        command.base, command.As<RumAddViewAttributePayload>()
    );
  }

  // On `RemoveViewAttribute`, any attribute value matching the given name should be
  // removed from the active view
  if (command.Is<RumRemoveViewAttributePayload>()) {
    return HandleRemoveViewAttribute(
        command.base, command.As<RumRemoveViewAttributePayload>()
    );
  }

  // On `AddAction`, we should immediately process the action if its type is `custom`,
  // or for any other action type we should either a.) open a new action scope with
  // is_continuous false, or b.) drop the action if we already have an active action
  if (command.Is<RumAddActionPayload>()) {
    return HandleAddAction(command.base, command.As<RumAddActionPayload>());
  }

  // On `StartAction`, we should either a.) open a new action scope with is_continuous
  // true, or b.) drop the action if we already have an active action
  if (command.Is<RumStartActionPayload>()) {
    return HandleStartAction(command.base, command.As<RumStartActionPayload>());
  }

  // TODO(RUM-12202): Handle StartResource (while _is_active) by opening a child
  // resource scope and inserting it into our _resource_scopes lookup by key

  return ViewEventType::None;
}

RumViewScope::ViewEventType RumViewScope::HandleStopSession(
    const RumCommandParams& base
) {
  // If already inactive, we can ignore StopSession
  if (!_is_active) {
    return ViewEventType::None;
  }

  // TODO(RUM-12202): If the view has pending resources on StopSession, they will
  // never receive StopResource, so there may not be any sense in keeping the session
  // scope open (while inactive) in this case

  // If this view is still active, mark it inactive and ensure that it sends a final
  // event
  const bool accept_command_attributes = false;
  BecomeInactive(base, accept_command_attributes);
  return ViewEventType::Full;
}

RumViewScope::ViewEventType RumViewScope::HandleStartView(
    const RumCommandParams& base, const RumStartViewPayload& payload
) {
  // If already inactive, we can ignore StartView
  if (!_is_active) {
    return ViewEventType::None;
  }

  // If this is the first StartView command we've ever seen, and it matches our view
  // key, then _we_ are the newly-created view and we should update our state
  const bool is_new_view_scope = payload.key == _key && !_has_processed_start_view;
  if (is_new_view_scope) {
    // If the StartView command has a set of attributes associated with it, adopt those
    // values as our initial view-level attributes
    if (base.attributes.GetType() == ValueType::Object) {
      _view_attributes.attribute = base.attributes;
      // _global_and_view_attributes can be left as-is in this case; it'll be populated
      // from the latest command when we send our first event
    }

    // Ensure that future StartView calls with the same key will close this scope
    _has_processed_start_view = true;

    // Ensure that we send an initial view event
    return ViewEventType::Full;
  }

  // Otherwise, the StartView command was issued for a different view scope: so the
  // attributes in the command describe a different view and should not be accepted into
  // this scope, but we should wrap up this view scope
  const bool accept_command_attributes = false;
  BecomeInactive(base, accept_command_attributes);
  return ViewEventType::Full;
}

RumViewScope::ViewEventType RumViewScope::HandleStopView(
    const RumCommandParams& base, const RumStopViewPayload& payload
) {
  // If already inactive, we can ignore StopView
  if (!_is_active) {
    return ViewEventType::None;
  }

  // If the command targets a different view key, we can ignore StopView
  if (payload.key != _key) {
    return ViewEventType::None;
  }

  // Otherwise, we are the active view and we're being commanded to stop: merge in any
  // attributes that were provided on StopView so they'll be present in the final
  // event
  const bool accept_command_attributes = true;
  BecomeInactive(base, accept_command_attributes);
  return ViewEventType::Full;
}

RumViewScope::ViewEventType RumViewScope::HandleAddViewAttribute(
    const RumCommandParams&, const RumAddViewAttributePayload& payload
) {
  // Once is a view becomes inactive, its attributes remain frozen
  if (!_is_active) {
    return ViewEventType::None;
  }

  // Mutate view-level attributes: there's no need to pre-merge into
  // _global_and_view_attributes here, since while the view is active we will perform
  // that merge immediately before generating events
  _view_attributes.attribute.SetObjectProperty(payload.name, payload.value);
  return ViewEventType::None;
}

RumViewScope::ViewEventType RumViewScope::HandleRemoveViewAttribute(
    const RumCommandParams&, const RumRemoveViewAttributePayload& payload
) {
  // Once is a view becomes inactive, its attributes remain frozen
  if (!_is_active) {
    return ViewEventType::None;
  }

  // Mutate view-level attributes: as with HandleAddViewAttribute, no need to merge
  _view_attributes.attribute.DeleteObjectProperty(payload.name);
  return ViewEventType::None;
}

RumViewScope::ViewEventType RumViewScope::HandleAddAction(
    const RumCommandParams& base, const RumAddActionPayload& payload
) {
  // If already inactive, we can ignore AddAction
  if (!_is_active) {
    return ViewEventType::None;
  }

  // A discrete custom action should always be recorded immediately
  if (payload.type == RumActionType::Custom) {
    ProcessDiscreteCustomAction(base, payload.name);
    return ViewEventType::Full;
  }

  // If we still have an active action
  if (_active_action_scope) {
    LogDroppedAction(payload.type, payload.name);
    return ViewEventType::None;
  }

  const bool is_continuous = false;
  OpenActionScope(base, payload.type, payload.name, is_continuous);
  return ViewEventType::None;
}

RumViewScope::ViewEventType RumViewScope::HandleStartAction(
    const RumCommandParams& base, const RumStartActionPayload& payload
) {
  // If already inactive, we can ignore StartAction
  if (!_is_active) {
    return ViewEventType::None;
  }

  if (_active_action_scope) {
    LogDroppedAction(payload.type, payload.name);
    return ViewEventType::None;
  }

  const bool is_continuous = true;
  OpenActionScope(base, payload.type, payload.name, is_continuous);
  return ViewEventType::None;
}

void RumViewScope::BecomeInactive(
    const RumCommandParams& base, bool accept_command_attributes
) {
  // Clear the _is_active flag: this indicates that the view should no longer process
  // any commands that affect view lifecycle, attributes, etc.; although the scope may
  // remain open to finish processing commands for pending resources
  DATADOG_ASSERT(_is_active, "BecomeInactive called while already inactive");
  _is_active = false;

  // Store the timestamp at which we became inactive: now that we're inactive, any
  // further view events will have their 'time_spent' count frozen based on this value
  _rendered_inactive_at = base.issued_at;

  // Now that we're no longer active, we want to freeze the set of attributes that are
  // included in events for this view: i.e. subsequent changes to global or view-level
  // attributes should be IGNORED by this view scope. This requires us to store our
  // final set of merged attributes as of view-stop-time, optionally including the
  // command attributes in the case of StopView.
  const Attribute& global_attributes = base.global_attributes;
  const Attribute& view_attributes = _view_attributes.attribute;
  const Attribute& extra = accept_command_attributes ? base.attributes : Attribute();
  AttributeMerge::AssembleObject(
      _global_and_view_attributes.attribute, {global_attributes, view_attributes, extra}
  );
}

void RumViewScope::ProcessDiscreteCustomAction(
    const RumCommandParams& base, std::string_view name
) {
  const RumScopeDependencies& deps = _deps;
  const UUID action_id = UUID::Random();
  RumActionScope scope(
      deps,
      *this,
      action_id,
      RumActionType::Custom,
      name,
      base.issued_at,
      std::chrono::milliseconds(100),
      base.attributes
  );

  RumCommandParams base_copy = base;
  const RumScopeResult result =
      scope.Process(RumCommand::StopAction(std::move(base_copy), name));

  DATADOG_ASSERT(
      result == RumScopeResult::Close,
      "Scope for discrete user action did not close in response to StopAction"
  );
  DATADOG_ASSERT(
      scope.HasSentActionEvent(),
      "Scope for discrete user action did not sent event in response to StopAction"
  );

  _num_actions_completed++;
}

void RumViewScope::OpenActionScope(
    const RumCommandParams& base,
    RumActionType type,
    std::string_view name,
    bool is_continuous
) {
  DATADOG_ASSERT(
      _active_action_scope == std::nullopt,
      "OpenActionScope called with existing _active_action_scope"
  );

  const RumScopeDependencies& deps = _deps;
  const UUID action_id = UUID::Random();
  const Duration timeout_duration =
      is_continuous ? std::chrono::seconds(10) : std::chrono::milliseconds(100);
  _active_action_scope.emplace(
      deps,
      *this,
      action_id,
      type,
      name,
      base.issued_at,
      timeout_duration,
      base.attributes
  );
}

void RumViewScope::LogDroppedAction(RumActionType type, std::string_view name) const {
  const RumScopeDependencies& deps = _deps;
  const size_t i = static_cast<size_t>(type);
  std::string_view type_str =
      i < std::size(RumActionTypeValues) ? RumActionTypeValues[i].name : "";  // NOLINT
  deps.diagnostic_logger.Warning(
      "RUM action dropped: a view may only have one active action at any given time",
      {{"action_type", type_str}, {"action_name", name}}
  );
}

void RumViewScope::SendViewEvent(const RumCommand& command) {
  // Resolve references needed to populate required event data
  const RumScopeDependencies& deps = _deps;
  const RumSessionScope& session = _parent;

  // The 'date' timestamp on a RUM 'view' event always indicates the time at which the
  // view started: it does NOT necessarily reflect the time at which the event was sent
  const Timestamp event_timestamp = _started_at;

  // Compute elapsed time between view start and the processing of this command
  const Timestamp end = _is_active ? command.base.issued_at : _rendered_inactive_at;
  const Duration time_spent = end - _started_at;
  const uint64_t time_spent_ns = time_spent.count();

  // Get sums of child scope occurrences within the lifetime of this view
  const uint64_t action_count = _num_actions_completed;
  const uint64_t error_count = 0;     // TODO(RUM-12201)
  const uint64_t resource_count = 0;  // TODO(RUM-12202)

  // Construct an event value on the stack with the minimal set of required properties
  RumViewEvent ev(
      event_timestamp,
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

  // Set 'context' to the full set of user-specified attributes that should be included
  // in this event. If the view is still active, we resolve the current set of global
  // attribute values carried with the command; if the view is inactive, we ignore those
  // values and use the last known set of attribute values that we stored when the view
  // became inactive.
  if (_is_active) {
    AttributeMerge::AssembleObject(
        _global_and_view_attributes.attribute,
        {command.base.global_attributes, _view_attributes.attribute}
    );
  }
  if (_global_and_view_attributes.attribute.GetObjectPropertyCount() > 0) {
    ev.context.value = _global_and_view_attributes.attribute;
  }

  // Serialize the event to JSON in a shared buffer, then copy that raw event payload
  // onto the storage thread
  deps.ProduceEvent(ev);
}

ScopeRef<const RumActionScope> RumViewScope::GetActiveAction() const {
  return _active_action_scope;
}

}  // namespace datadog::impl
