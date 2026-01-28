// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/features/rum/scopes/resource.hpp"

#include "datadog/impl/assert.hpp"
#include "datadog/impl/attribute/merge.hpp"
#include "datadog/impl/features/rum/context.hpp"
#include "datadog/impl/features/rum/scopes/event_enrichment.hpp"
#include "datadog/impl/features/rum/scopes/session.hpp"
#include "datadog/impl/features/rum/scopes/view.hpp"

namespace datadog::impl {

RumResourceScope::RumResourceScope(
    const RumScopeDependencies& deps,
    RumViewScope& parent,
    const UUID& resource_id,
    std::string_view key,
    RumResourceMethod method,
    std::string_view url,
    Timestamp start_time,
    const Attribute& attributes
)
    : _deps(deps),
      _parent(parent),
      _resource_id(resource_id),
      _key(key),
      _method(method),
      _url(url),
      _started_at(start_time),
      _attributes(attributes) {
  // Cache the ID of the active action, if any action scope is currently open. This
  // allows us to correlate any RUM resource/error events that we produce with the
  // action, even in the case of a StopResource command that clears the active action
  const auto& active_action = parent.GetActiveAction();
  if (active_action) {
    _active_action_id_on_resource_start = active_action->get().GetActionID();
  }
}

RumScopeResult RumResourceScope::Process(const RumCommand& command) {
  // Note that RumViewScope generally only propagates commands that explicitly target a
  // specific resource, and it will propagate such a command only to the scope
  // identified by `key` in the command. Do not expect RumResourceScope to receive any
  // other types of commands.

  // On StopResource, if the command targets this resource, we should close the scope
  // and send an event (either 'resource' or 'error' depending on whether the resource
  // completed without error)
  if (command.Is<RumStopResourcePayload>()) {
    const auto& payload = command.As<RumStopResourcePayload>();

    // If we've been given a StopResource command that targets a different resource,
    // ignore the command
    if (payload.key != _key) {
      return RumScopeResult::RemainOpen;
    }

    // If the resource was stopped with a StopResourceWithError call, we should send an
    // 'error' event whose 'error.resource' properties describe this request, then close
    // the scope (wihthout sending a 'resource' event)
    if (payload.error) {
      SendErrorEvent(command.base, payload, *payload.error);
      return RumScopeResult::Close;
    }

    // Otherwise, StopResource was called, indicating that the application got a valid
    // response: send a 'resource' event and close the scope
    SendResourceEvent(command.base, payload);
    return RumScopeResult::Close;
  }

  // If we've been given a command of any other type, ignore it and remain open waiting
  // for StopResource
  return RumScopeResult::RemainOpen;
}

void RumResourceScope::SendResourceEvent(
    const RumCommandParams& base, const RumStopResourcePayload& payload
) {
  // A resource scope should only ever send a single event
  DATADOG_ASSERT(
      _result == Result::SentNoEvent,
      "SendResourceEvent called after a previous event was sent"
  );

  // Resolve references needed to populate required event data
  const RumScopeDependencies& deps = _deps;
  const RumViewScope& view = _parent;
  const RumSessionScope& session = view.GetParentSessionScope();

  // The 'date' timestamp on a RUM 'resource' event indicates when the resource started,
  // not when we're sending it: i.e. this timestamp indicates when StartResource was
  // called
  const Timestamp event_timestamp = _started_at;

  // The 'duration' property indicates how much time elapsed, in nanoseconds, between
  // StartResource and StopResource
  const Duration resource_duration = base.issued_at - _started_at;

  // We're sending this event upon successful completion of the request, so pull in the
  // relevant property values from the HTTP response details
  RumResourceType resource_type = payload.response.resource_type;
  std::optional<int32_t> status_code_opt;
  if (payload.response.status_code > 0) {
    status_code_opt = payload.response.status_code;
  }
  std::optional<int64_t> size_opt;
  if (payload.response.size >= 0) {
    size_opt = payload.response.size;
  }

  // Construct an event value on the stack with the minimal set of required properties
  RumResourceEvent ev(
      event_timestamp,
      deps.application_id,
      session.GetSessionID(),
      RumSessionType::User,
      view.GetViewID(),
      view.GetKey(),
      _resource_id,
      resource_type,
      _url
  );

  // Set essential view properties
  if (!view.GetName().empty()) {
    ev.view.name.value = view.GetName();
  }

  // Correlate this event with the active action, if the parent view had one at the time
  // this resource was started
  if (_active_action_id_on_resource_start != UUID::Zero) {
    ev.action.value.emplace(_active_action_id_on_resource_start);
  }

  // Set essential resource properties
  ev.resource.method = _method;
  ev.resource.duration = resource_duration.count();  // Nanoseconds elapsed
  ev.resource.status_code = status_code_opt;
  ev.resource.size = size_opt;

  // Set 'context' to the full set of user-specified attributes that should be included
  // in this event, including global and view attributes, as well as any resource-level
  // attributes specified in StartResource and/or StopResource
  Attribute context = MergeAttributesForEventContext(base);
  if (context.GetObjectPropertyCount() > 0) {
    ev.context.value = context;
  }

  // Enrich event with OS properties from CoreContext
  RumEventEnrichment::PopulateOsProperties(deps.scope, ev);

  deps.ProduceEvent(ev);
  _result = Result::SentResourceEvent;
}

void RumResourceScope::SendErrorEvent(
    const RumCommandParams& base,
    const RumStopResourcePayload& payload,
    const RumErrorDetails& error
) {
  // A resource scope should only ever send a single event
  DATADOG_ASSERT(
      _result == Result::SentNoEvent, "SendError called after a previous event was sent"
  );

  // Resolve references needed to populate required event data
  const RumScopeDependencies& deps = _deps;
  const RumViewScope& view = _parent;
  const RumSessionScope& session = view.GetParentSessionScope();

  // The 'date' timestamp on a RUM 'error' event indicates when the error occurred: i.e.
  // this timestamp indicates when StopResourceWithError was called. The start time and
  // duration of the resource are not recorded in this case.
  const Timestamp event_timestamp = base.issued_at;

  // Construct an event value on the stack with the minimal set of required properties
  RumErrorEvent ev(
      event_timestamp,
      deps.application_id,
      session.GetSessionID(),
      RumSessionType::User,
      view.GetViewID(),
      view.GetKey(),
      error.message,
      RumErrorSource::Network  // As per dd-sdk-ios, 'error.source' is always 'network'
                               // for resource errors
  );

  // Set essential view properties
  if (!view.GetName().empty()) {
    ev.view.name.value = view.GetName();
  }

  // Correlate this event with the active action, if the parent view had one at the time
  // this resource was started
  if (_active_action_id_on_resource_start != UUID::Zero) {
    ev.action.value.emplace(_active_action_id_on_resource_start);
  }

  // Set essential error details
  ev.error.type = error.type;
  ev.error.stack = error.stack_trace;
  if (error.is_network_error) {
    ev.error.category = RumErrorCategory::Network;
  } else {
    ev.error.category = RumErrorCategory::Exception;
  }

  // Set essential resource details
  ev.error.resource.value.emplace(_method, payload.response.status_code, _url);

  // Set 'context' to the full set of user-specified attributes that should be included
  // in this event, including global and view attributes, as well as any resource-level
  // attributes specified in StartResource and/or StopResourceWithError
  Attribute context = MergeAttributesForEventContext(base);
  if (context.GetObjectPropertyCount() > 0) {
    ev.context.value = context;
  }

  // Enrich event with OS properties from CoreContext
  RumEventEnrichment::PopulateOsProperties(deps.scope, ev);

  deps.ProduceEvent(ev);
  _result = Result::SentErrorEvent;
}

UUID RumResourceScope::ResolveActiveActionId(const RumViewScope& parent) {
  // Interrogate the parent view scope to get the active action ID, or UUID::Zero
  auto action_opt = parent.GetActiveAction();
  if (action_opt) {
    return action_opt->get().GetActionID();
  }
  return UUID::Zero;
}

Attribute RumResourceScope::MergeAttributesForEventContext(
    const RumCommandParams& base
) {
  // For both 'resource' and 'error' events, we resolve the final set of 'context'
  // values by merging attributes in this order:
  // 1. Global RUM attributes as of the time the event is generated
  // 2. Parent View attributes as of the time the event is generated
  // 3. Any resource-level attributes previously provided on StartResource
  // 4. Any resource-level attributes added or updated on StopResource[WithError]
  const Attribute& global_attributes = base.global_attributes;
  Attribute view_attributes = _parent.get().GetAttributes();
  const Attribute& existing_resource_attributes = _attributes;
  const Attribute& new_resource_attributes = base.attributes;

  Attribute merged_attributes = Attribute::Object();
  AttributeMerge::AssembleObject(
      merged_attributes,
      {global_attributes,
       view_attributes,
       existing_resource_attributes,
       new_resource_attributes}
  );
  return merged_attributes;
}

}  // namespace datadog::impl
