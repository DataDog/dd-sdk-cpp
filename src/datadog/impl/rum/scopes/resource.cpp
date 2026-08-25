// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/rum/scopes/resource.hpp"

#include "datadog/impl/core/attribute/merge.hpp"
#include "datadog/impl/rum/context.hpp"
#include "datadog/impl/rum/scopes/event_enrichment.hpp"
#include "datadog/impl/rum/scopes/session.hpp"
#include "datadog/impl/rum/scopes/view.hpp"
#include "datadog/impl/types/assert.hpp"

namespace datadog::impl {

/**
 * Extracts the four cross-platform tracing attributes from the merged event `context`,
 * populating `dd` and removing the keys from `context` so they don't appear in the
 * event's 'context' field.
 *
 * To avoid consuming global or view attributes that happen to share a key name, each
 * key is only extracted if it is present in at least one of `start_attrs` or
 * `stop_attrs` — the per-resource attribute sets accumulated from StartResource and
 * StopResource[WithError] calls. This check requires no copies: `start_attrs` and
 * `stop_attrs` are read-only; only `context` (the already-merged result) is mutated.
 * Works for any Internal struct that carries span_id, parent_span_id, trace_id, and
 * rule_psr (resource and error).
 */
template <typename Internal>
static void extract_resource_trace_attributes(
    const Attribute& start_attrs,
    const Attribute& stop_attrs,
    Attribute& context,
    Internal& dd
) {
  auto extract_string = [&](std::string_view key, OmitIfEmpty<std::string>& field) {
    if (start_attrs.FindObjectProperty(key) < 0 &&
        stop_attrs.FindObjectProperty(key) < 0) {
      return;  // not a resource-level attr; leave in context
    }
    const Attribute val = context.GetObjectProperty(key);
    if (val.GetType() == ValueType::String) {
      field = std::string(val.GetStringValue());
    }
    context.DeleteObjectProperty(key);
  };

  extract_string("_dd.trace_id", dd.trace_id);
  extract_string("_dd.span_id", dd.span_id);
  extract_string("_dd.parent_span_id", dd.parent_span_id);

  if (start_attrs.FindObjectProperty("_dd.rule_psr") >= 0 ||
      stop_attrs.FindObjectProperty("_dd.rule_psr") >= 0) {
    const Attribute val = context.GetObjectProperty("_dd.rule_psr");
    if (val.GetType() == ValueType::Double) {
      dd.rule_psr = static_cast<float>(val.GetDoubleValue());
    }
    context.DeleteObjectProperty("_dd.rule_psr");
  }
}

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

RumScopeResult RumResourceScope::Process(
    const RumCommand& command, const CoreContext& context, const EventWriter& writer
) {
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
      SendErrorEvent(command.base, payload, *payload.error, context, writer);
      return RumScopeResult::Close;
    }

    // Otherwise, StopResource was called, indicating that the application got a valid
    // response: send a 'resource' event and close the scope
    SendResourceEvent(command.base, payload, context, writer);
    return RumScopeResult::Close;
  }

  // If we've been given a command of any other type, ignore it and remain open waiting
  // for StopResource
  return RumScopeResult::RemainOpen;
}

void RumResourceScope::SendResourceEvent(
    const RumCommandParams& base,
    const RumStopResourcePayload& payload,
    const CoreContext& context_param,
    const EventWriter& writer
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
  // attributes specified in StartResource and/or StopResource. After merging, extract
  // any cross-platform tracing attributes that originated from the per-resource sets
  // so they land on ev._dd rather than in context.
  Attribute context = MergeAttributesForEventContext(base);
  extract_resource_trace_attributes(_attributes, base.attributes, context, ev._dd);
  if (context.GetObjectPropertyCount() > 0) {
    ev.context.value = context;
  }

  // Enrich event with OS and device properties from CoreContext
  RumEventEnrichment::PopulateCommonProperties(context_param, ev);

  std::string_view json = deps.EncodeEvent(ev);
  const bool bypass_tracking_consent = false;
  writer(Block{json.data(), json.size()}, Block{}, bypass_tracking_consent);
  _result = Result::SentResourceEvent;
}

void RumResourceScope::SendErrorEvent(
    const RumCommandParams& base,
    const RumStopResourcePayload& payload,
    const RumErrorDetails& error,
    const CoreContext& context_param,
    const EventWriter& writer
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
  // attributes specified in StartResource and/or StopResourceWithError. After merging,
  // extract any cross-platform tracing attributes that originated from the per-resource
  // sets so they land on ev._dd rather than in context.
  Attribute context = MergeAttributesForEventContext(base);
  extract_resource_trace_attributes(_attributes, base.attributes, context, ev._dd);

  // Extract a `_dd.error.source_type` override, if supplied alongside StartResource or
  // StopResourceWithError, and strip it from the attributes that end up in 'context'
  ev.error.source_type = ExtractErrorSourceType(
      {_attributes, base.attributes}, context, deps.diagnostic_logger
  );

  if (context.GetObjectPropertyCount() > 0) {
    ev.context.value = context;
  }

  // Enrich event with OS and device properties from CoreContext
  RumEventEnrichment::PopulateCommonProperties(context_param, ev);

  std::string_view json = deps.EncodeEvent(ev);
  const bool bypass_tracking_consent = false;
  writer(Block{json.data(), json.size()}, Block{}, bypass_tracking_consent);
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
