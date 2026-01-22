// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/features/rum/scopes/action.hpp"

#include "datadog/impl/attribute/merge.hpp"
#include "datadog/impl/features/rum/context.hpp"
#include "datadog/impl/features/rum/scopes/session.hpp"
#include "datadog/impl/features/rum/scopes/view.hpp"

namespace datadog::impl {

RumActionScope::RumActionScope(
    const RumScopeDependencies& deps,
    RumViewScope& parent,
    const UUID& action_id,
    RumActionType type,
    std::string_view name,
    Timestamp start_time,
    Duration timeout_duration,
    const Attribute& attributes
)
    : _deps(deps),
      _parent(parent),
      _action_id(action_id),
      _type(type),
      _name(name),
      _started_at(start_time),
      _expires_at(start_time + timeout_duration),
      _attributes(attributes) {}

void RumActionScope::PopulateContext(struct RumContext& out_context) const {
  // Call the parent's PopulateContext function to set application, session, and view
  // details
  const RumViewScope& parent = _parent;
  parent.PopulateContext(out_context);

  // Set action-specific parameters: assume that if we're being asked to populate a
  // RumContext, we are in fact the active action
  out_context.active_action_id = _action_id;
}

RumScopeResult RumActionScope::Process(const RumCommand& command) {
  // Determine whether our expiration time has passed: even if it has, we still remain
  // active until the last active resource that started under our watch has ended
  const bool has_expired = command.base.issued_at >= _expires_at;

  // On StopResource: decrement our active resource count, but ignore the command if we
  // have no active resources
  if (command.Is<RumStopResourcePayload>() && _num_active_resources > 0) {
    _num_resources_recorded++;
    _num_active_resources--;

    // If the resource in question is being stopped due to an error, record the fact
    // that an error has occurred while this action was active
    if (command.As<RumStopResourcePayload>().error) {
      _num_errors_recorded++;
    }
  }

  // Determine if we should consider the action completed and close this scope: if our
  // expiration time has passed and we're not waiting on any resource calls to finish,
  // we should close and process no further commands
  bool should_close = has_expired && _num_active_resources <= 0;

  // If we've received an explicit StopAction command, we should always close the scope
  // and send an action event, regardless of expiration time or active resources
  if (command.Is<RumStopActionPayload>()) {
    // At the API layer, we require `StopAction(type, name)`, but these values are not
    // used to *identify* the action to be stopped. Instead, `type` is entirely ignored
    // and `name` is used to rename the currently active action when it's stopped.
    std::string_view new_name = command.As<RumStopActionPayload>().name;
    if (!new_name.empty()) {
      _name = new_name;
    }
    should_close = true;
  }

  // Similarly, if the current view is changing, this view is about to be stopped, so
  // this action should stop as well; and likewise, StopSession also ends the view
  if (command.Is<RumStartViewPayload>() || command.Is<RumStopViewPayload>() ||
      command.Is<RumStopSessionPayload>()) {
    should_close = true;
  }

  // If it's time to close, send a RUM event to describe this action, then close the
  // scope
  if (should_close) {
    // If the action is being completed past its expiration time, backdate its
    // completion to that time: this will be reflected in the event payload as a
    // duration in nanoseconds called `loading_time`
    const Timestamp completed_at = has_expired ? _expires_at : command.base.issued_at;
    SendActionEvent(command, completed_at);
    return RumScopeResult::Close;
  }

  // On StartResource: consider the resource relevant to this action, keeping the action
  // active and potentially extending the lifetime of this scope until the resource is
  // finished
  if (command.Is<RumStartResourcePayload>()) {
    _num_active_resources++;
  }

  // On AddError: increment the count of errors recorded while this view was active
  if (command.Is<RumAddErrorPayload>()) {
    _num_errors_recorded++;
  }

  // The action remains active
  return RumScopeResult::RemainOpen;
}

void RumActionScope::SendActionEvent(
    const RumCommand& command, const Timestamp& completed_at
) {
  // Resolve references needed to populate required event data
  const RumScopeDependencies& deps = _deps;
  const RumViewScope& view = _parent;
  const RumSessionScope& session = view.GetParentSessionScope();

  // The 'date' timestamp on a RUM 'action' event indicates when the action started, not
  // when we're sending it
  const Timestamp event_timestamp = _started_at;

  // The oddly-named 'loading_time' property indicates how long the action lasted, which
  // gets backdated to the action scope's original expiration time even if it stayed
  // open longer to wait for pending resources
  Duration action_duration{};
  if (completed_at > _started_at) {
    action_duration = completed_at - _started_at;
  }

  // Construct an event value on the stack with the minimal set of required properties
  RumActionEvent ev(
      event_timestamp,
      deps.application_id,
      session.GetSessionID(),
      RumSessionType::User,
      view.GetViewID(),
      view.GetKey(),
      _type,
      _action_id,
      action_duration
  );

  // Set essential view properties
  if (!view.GetName().empty()) {
    ev.view.name.value = view.GetName();
  }

  // Set essential action properties
  ev.action.target.value.emplace(_name);

  if (_num_resources_recorded > 0) {
    ev.action.resource.value.emplace(_num_resources_recorded);
  }
  if (_num_errors_recorded > 0) {
    ev.action.error.value.emplace(_num_errors_recorded);
  }

  // Prepare to merge the final set of custom attributes for our action event, in this
  // order:
  // 1. Global RUM attributes as of the time the event is generated
  // 2. Parent View attributes as of the time the event is generated
  // 3. Any action-level attributes previously provided on AddAction/StartAction
  // 4. Any action-level attributes added or updated on the call to StopAction
  const Attribute& global_attributes = command.base.global_attributes;
  Attribute view_attributes = view.GetAttributes();
  const Attribute& existing_action_attributes = _attributes;
  Attribute new_action_attributes;
  if (command.Is<RumStopActionPayload>()) {
    new_action_attributes = command.base.attributes;
  }

  // Create a merged set of user attributes to include in the event, but skip this step
  // if we can just use _attributes as-is
  const Attribute* context = nullptr;
  Attribute merged_attributes = Attribute::Object();
  const size_t num_other_attributes = global_attributes.GetObjectPropertyCount() +
                                      view_attributes.GetObjectPropertyCount() +
                                      new_action_attributes.GetObjectPropertyCount();
  if (num_other_attributes > 0) {
    AttributeMerge::AssembleObject(
        merged_attributes,
        {global_attributes,
         view_attributes,
         existing_action_attributes,
         new_action_attributes}
    );
    context = &merged_attributes;
  } else {
    context = &existing_action_attributes;
  }

  // Set 'context' to the full set of user-specified attributes that should be included
  // in this event, including global and view attributes, as well as any action-level
  // attributes specified in AddAction/StartAction and/or StopAction
  if (context && context->GetObjectPropertyCount() > 0) {
    ev.context.value = *context;
  }

  deps.ProduceEvent(ev);
  _has_sent_action_event = true;
}

}  // namespace datadog::impl
