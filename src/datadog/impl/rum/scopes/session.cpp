// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/rum/scopes/session.hpp"

#include <algorithm>
#include <string>

#include "datadog/impl/core/attribute/merge.hpp"
#include "datadog/impl/core/context.hpp"
#include "datadog/impl/rum/context.hpp"
#include "datadog/impl/rum/scopes/application.hpp"
#include "datadog/impl/rum/scopes/event_enrichment.hpp"
#include "datadog/impl/types/assert.hpp"

namespace datadog::impl {

const Duration RumSessionScope::INACTIVITY_TIMEOUT_DURATION = std::chrono::minutes(15);
const Duration RumSessionScope::MAX_SESSION_DURATION = std::chrono::hours(4);

RumSessionScope::RumSessionScope(
    const RumScopeDependencies& deps,
    RumApplicationScope& parent,
    bool is_initial_session,
    bool is_sampled,
    const UUID& session_id,
    RumSessionPrecondition start_precondition,
    Timestamp start_time,
    const std::optional<RumSessionScope::ViewDetails>& active_view_from_predecessor
)
    : _deps(deps),
      _parent(parent),
      _is_initial_session(is_initial_session),
      _is_sampled(is_sampled),
      _session_id(session_id),
      _precondition(start_precondition),
      _started_at(start_time),
      _last_interaction_at(start_time),
      _active_view_from_predecessor(active_view_from_predecessor),
      _view_scopes(deps.diagnostic_logger) {}

void RumSessionScope::PopulateContext(RumContext& out_context) const {
  // Call the parent's PopulateContext function to set application parameters
  const RumApplicationScope& parent = _parent;
  parent.PopulateContext(out_context);

  // Set session-specific parameters
  out_context.session_id = _session_id;
  out_context.session_is_sampled = _is_sampled;
  out_context.session_is_active = !_end_reason.has_value();
  out_context.session_precondition = _precondition;
}

RumScopeResult RumSessionScope::Process(
    const RumCommand& command, const CoreContext& context, const EventWriter& writer
) {
  // -- Determine if the session needs to end, and early-out if so

  // If the session already ended in response to a prior command, we should not be
  // receiving any new commands: we should have been closed immediately and removed from
  // the application scope
  DATADOG_ASSERT(
      !_end_reason.has_value(),
      "session scope has a valid EndReason at the start of command processing"
  );

  // If this command either explicitly stops the session or is arriving after the user
  // inactivity timeout or the maximum session duration have been exceeded, close the
  // session
  const auto should_close = ShouldCloseRatherThanProcessing(command);
  if (should_close) {
    // Before returning control to the application scope, record some state to
    // facilitate view transfer, and (only if explicitly stopped) send final view events
    _end_reason = should_close;
    OnClose(command, *_end_reason, context, writer);
    return RumScopeResult::Close;
  }

  // -- Refresh inactivity timeout

  // If the command is a user interaction, refresh our last-interaction timestamp to
  // reset the clock on the inactivity timeout
  if (command.HasFlag(RumCommandFlags::UserInteraction)) {
    _last_interaction_at = command.base.issued_at;
  }

  // -- Early-out if session not sampled

  // For sessions that are excluded from sampling, we don't bother keeping track of
  // state for views etc. - since no events will be stored or uploaded in conjunction
  // with this session, there's no need to do any further command processing
  if (!_is_sampled) {
    return RumScopeResult::RemainOpen;
  }

  // -- Deal with post-session-refresh view transfer

  // If we're still holding on to the details of the last active view from the previous
  // session and we get a StopView call targeting that view, clear those details so we
  // won't recreate a stopped view
  if (_active_view_from_predecessor && command.Is<RumStopViewPayload>()) {
    const auto& payload = command.As<RumStopViewPayload>();
    if (payload.key == _active_view_from_predecessor->key) {
      _active_view_from_predecessor.reset();
    }
  }

  // If we're holding on to last-active-view details from the previous session and this
  // command requires an active view, initiate view transfer if needed, and clear the
  // view details regardless
  if (_active_view_from_predecessor &&
      command.HasFlag(RumCommandFlags::RequiresActiveView)) {
    // Attempt to perform a "view transfer", which creates a new view scope with the
    // same basic details as the view that was active when our previous session ended:
    // this will be a no-op if we already have an active view or have previously created
    // any views in this session
    AttemptViewTransfer(command, *_active_view_from_predecessor, context, writer);

    // Clear last view state, regardless of whether we successfully created a new view,
    // so we don't run these checks again
    _active_view_from_predecessor.reset();
  }

  // -- Manage child views and propagate commands to views

  // TODO(RUM-12242): If the command is 'ApplicationStart', create ApplicationLaunch
  // view if warranted

  // If the command is 'StartView', open a new view scope and add it to the array of
  // child views
  if (command.Is<RumStartViewPayload>()) {
    // Get basic view scope details
    const auto& payload = command.As<RumStartViewPayload>();
    const bool is_initial_view = false;
    const UUID view_id = UUID::Random();

    // Open a new scope: the command-propagation logic below will ensure that the
    // StartView command is handled by this new scope as well as any others
    _view_scopes.Push(
        _deps,
        *this,
        is_initial_view,
        view_id,
        payload.key,
        payload.name,
        command.base.issued_at
    );
    _num_views_opened++;
  }

  // -- Handle TTID app-launch vital event (session-scoped, not delegated to views)

  if (command.Is<RumReportAppDisplayInitializedPayload>()) {
    HandleReportAppDisplayInitialized(command, context, writer);
    return RumScopeResult::RemainOpen;
  }

  // -- Handle TTFD app-launch vital event (session-scoped, not delegated to views)

  if (command.Is<RumReportAppFullyDisplayedPayload>()) {
    HandleReportAppFullyDisplayed(command, context, writer);
    return RumScopeResult::RemainOpen;
  }

  // -- Handle operation vital events (session-scoped, not delegated to views)

  if (command.Is<RumStartOperationPayload>()) {
    const auto& payload = command.As<RumStartOperationPayload>();

    // Build lookup key for active operation tracking: name + operationKey
    std::string lookup_key(payload.name);
    if (payload.operation_key) {
      lookup_key.append(*payload.operation_key);
    }

    // Warn if the operation is already active (but never suppress the event)
    if (_active_operations.count(lookup_key) > 0) {
      if (payload.operation_key) {
        _deps.get().diagnostic_logger.Warning(
            "StartOperation called for operation that has already been started",
            {{"name", payload.name}, {"operation_key", *payload.operation_key}}
        );
      } else {
        _deps.get().diagnostic_logger.Warning(
            "StartOperation called for operation that has already been started",
            {{"name", payload.name}}
        );
      }
    }

    // Track this operation as active
    _active_operations.insert(std::move(lookup_key));

    // Emit a vital event with step_type = Start
    SendVitalEvent(
        command.base,
        payload.name,
        RumVitalStepType::Start,
        payload.operation_key,
        std::nullopt,
        context,
        writer
    );

    // Vital events are session-scoped: do not propagate to views
    return RumScopeResult::RemainOpen;
  }

  if (command.Is<RumStopOperationPayload>()) {
    const auto& payload = command.As<RumStopOperationPayload>();

    // Build lookup key for active operation tracking: name + operationKey
    std::string lookup_key(payload.name);
    if (payload.operation_key) {
      lookup_key.append(*payload.operation_key);
    }

    // Warn if the operation is not currently active (but never suppress the event)
    if (_active_operations.erase(lookup_key) == 0) {
      if (payload.operation_key) {
        _deps.get().diagnostic_logger.Warning(
            "StopOperation called for operation that is not currently active",
            {{"name", payload.name}, {"operation_key", *payload.operation_key}}
        );
      } else {
        _deps.get().diagnostic_logger.Warning(
            "StopOperation called for operation that is not currently active",
            {{"name", payload.name}}
        );
      }
    }

    // Map RumOperationFailureReason to RumVitalFailureReason
    std::optional<RumVitalFailureReason> vital_failure_reason;
    if (payload.failure_reason) {
      switch (*payload.failure_reason) {
        case RumOperationFailureReason::Error:
          vital_failure_reason = RumVitalFailureReason::Error;
          break;
        case RumOperationFailureReason::Abandoned:
          vital_failure_reason = RumVitalFailureReason::Abandoned;
          break;
        case RumOperationFailureReason::Other:
          vital_failure_reason = RumVitalFailureReason::Other;
          break;
      }
    }

    // Emit a vital event with step_type = End
    SendVitalEvent(
        command.base,
        payload.name,
        RumVitalStepType::End,
        payload.operation_key,
        vital_failure_reason,
        context,
        writer
    );

    // Vital events are session-scoped: do not propagate to views
    return RumScopeResult::RemainOpen;
  }

  // TODO(RUM-12242): In case of off-view commands, create ApplicationLaunch view if
  // warranted

  // TODO(RUM-11247): In case of off-view commands, create Background view if warranted

  // Propagate the command to any and all child view scopes
  _view_scopes.Propagate(command, context, writer);

  // All of the circumstances that cause a session to end are handled up-front in
  // ShouldCloseRatherThanProcessing(): no other command types should result in the
  // session being closed
  DATADOG_ASSERT(
      !_end_reason.has_value(),
      "session scope has a valid EndReason at the end of normal command processing"
  );
  return RumScopeResult::RemainOpen;
}

std::optional<RumSessionScope::EndReason>
RumSessionScope::ShouldCloseRatherThanProcessing(const RumCommand& command) const {
  // If it's been more than (e.g.) 15 minutes since the last user interaction, end this
  // session due to inactivity and go no further
  const Timestamp& now = command.base.issued_at;
  const Duration elapsed_since_last_interaction = now - _last_interaction_at;
  if (elapsed_since_last_interaction >= INACTIVITY_TIMEOUT_DURATION) {
    return EndReason::TimedOutDueToInactivity;
  }

  // If it's been more than (e.g.) 4 hours since the session started, end this session
  // and go no further
  const Duration elapsed_since_start = now - _started_at;
  if (elapsed_since_start > MAX_SESSION_DURATION) {
    return EndReason::ExceededMaxDuration;
  }

  // NOTE: Explicit stop should be handled after timeouts: if we process StopSession
  // when a session has passed its expiration thresholds; that's an expiration, not an
  // explicit stop.

  // If the command is 'StopSession', the user explicitly requested that we end the
  // session via a StopSession API call
  if (command.Is<RumStopSessionPayload>()) {
    return EndReason::Stopped;
  }

  // Session should remain open and attempt to process the command
  return std::nullopt;
}

void RumSessionScope::OnClose(
    const RumCommand& command,
    EndReason end_reason,
    const CoreContext& context,
    const EventWriter& writer
) {
  // If we have an active view, cache its essential details in _active_view_on_close so
  // they can be conveyed to the next session we might create
  DATADOG_ASSERT(!_active_view_on_close, "last active view already cached on close");
  const auto view_opt = GetActiveView();
  if (view_opt) {
    const RumViewScope& view = *view_opt;
    // TODO(RUM-12242): The 'ApplicationLaunch' view should not be transferred
    // TODO(RUM-12247): The synthetic 'Background' view should not be transferred
    _active_view_on_close.emplace(view.GetKey(), view.GetName(), view.GetAttributes());
  }

  // Clear active operation tracking on session close: the next session starts fresh
  _active_operations.clear();

  // If this session was explicitly stopped, propagate the StopSession command to child
  // views so they can be cleanly finalized and so that final view events can be sent:
  // note that we _don't_ send final view events after session expiration
  if (end_reason == EndReason::Stopped) {
    DATADOG_ASSERT(command.Is<RumStopSessionPayload>(), "stopped by non-StopSession");
    _view_scopes.Propagate(command, context, writer);
  }
}

void RumSessionScope::SendVitalEvent(
    const RumCommandParams& base,
    std::string_view name,
    RumVitalStepType step_type,
    const std::optional<std::string>& operation_key,
    std::optional<RumVitalFailureReason> failure_reason,
    const CoreContext& context,
    const EventWriter& writer
) {
  const RumScopeDependencies& deps = _deps;

  // Get view context: if there's an active view use its details, otherwise
  // fall back to zero UUID and empty url
  UUID view_id = UUID::Zero;
  std::string_view view_url;
  std::string_view view_name_str;
  Attribute view_attributes;
  const auto view_opt = GetActiveView();
  if (view_opt) {
    const RumViewScope& view = *view_opt;
    view_id = view.GetViewID();
    view_url = view.GetKey();
    view_name_str = view.GetName();
    view_attributes = view.GetAttributes();
  }

  // Construct the vital event with required fields
  const UUID vital_id = UUID::Random();
  RumVitalEvent ev(
      base.issued_at,
      deps.application_id,
      _session_id,
      RumSessionType::User,
      view_id,
      view_url,
      vital_id,
      RumVitalType::OperationStep,
      name,
      step_type
  );

  // Set optional view name
  if (!view_name_str.empty()) {
    ev.view.name.value = view_name_str;
  }

  // Set optional vital fields
  if (operation_key && !operation_key->empty()) {
    ev.vital.operation_key.value = *operation_key;
  }
  if (failure_reason) {
    ev.vital.failure_reason.value = *failure_reason;
  }

  // Set 'context' to the full set of user-specified attributes that should be
  // included in this event, merging: global <- view <- command
  Attribute merged_attributes = Attribute::Object();
  AttributeMerge::AssembleObject(
      merged_attributes, {base.global_attributes, view_attributes, base.attributes}
  );
  if (merged_attributes.GetObjectPropertyCount() > 0) {
    ev.context.value = merged_attributes;
  }

  // Enrich event with OS and device properties from CoreContext
  RumEventEnrichment::PopulateCommonProperties(context, ev);

  // Serialize and write the event
  std::string_view json = deps.EncodeEvent(ev);
  const bool bypass_tracking_consent = false;
  writer(Block{json.data(), json.size()}, Block{}, bypass_tracking_consent);
}

void RumSessionScope::HandleReportAppDisplayInitialized(
    const RumCommand& command, const CoreContext& context, const EventWriter& writer
) {
  const auto view_opt = GetActiveView();

  // Compute TTID duration: time from process launch to the moment of this call.
  const Timestamp& now = command.base.issued_at;
  const Timestamp& launch = context.process_launch_time;
  const double ttid_duration_ns = static_cast<double>((now - launch).count());

  static constexpr double kMaxTTIDNs = 60.0 * 1'000'000'000.0;
  if (ttid_duration_ns <= 0.0 || ttid_duration_ns >= kMaxTTIDNs) {
    _deps.get().diagnostic_logger.Warning(
        "Ignoring TTID; computed value falls outside expected range",
        {{"ttid_ns", ttid_duration_ns}}
    );
    return;
  }

  // If no view is active, emit the event with zero/empty view fields - matching
  // the behavior of the iOS and Android SDKs.
  const RumViewScope* view = view_opt ? &view_opt->get() : nullptr;

  // These handlers assume exactly-once delivery, enforced by the atomic guards in
  // Rum::ReportAppDisplayInitialized() and Rum::ReportAppFullyDisplayed(). Calling
  // this handler a second time would re-set _ttid_has_fired and re-emit.
  DATADOG_ASSERT(!_ttid_has_fired, "HandleReportAppDisplayInitialized called twice");

  SendAppLaunchVitalEvent(
      command.base,
      view,
      ttid_duration_ns,
      RumVitalAppLaunchMetric::TTID,
      "time_to_initial_display",
      context,
      writer
  );

  // TTID has now been recorded successfully.
  _ttid_has_fired = true;
  _ttid_duration_ns = ttid_duration_ns;

  // If ReportAppFullyDisplayed was called before this point, emit the deferred TTFD
  // event now. Use max(ttfd_raw, ttid) to clamp cases where the developer called
  // ReportAppFullyDisplayed before the first frame was drawn - matching iOS/Android
  // behavior.
  if (_pending_ttfd.has_value()) {
    _deps.get().diagnostic_logger.Warning(
        "ReportAppFullyDisplayed was called before ReportAppDisplayInitialized; "
        "TTFD duration will be clamped to TTID duration if necessary"
    );
    const double ttfd_duration_ns =
        std::max(_pending_ttfd->duration_ns, ttid_duration_ns);
    SendAppLaunchVitalEvent(
        _pending_ttfd->base,
        view,
        ttfd_duration_ns,
        RumVitalAppLaunchMetric::TTFD,
        "time_to_full_display",
        context,
        writer
    );
    _pending_ttfd.reset();
  }
}

void RumSessionScope::HandleReportAppFullyDisplayed(
    const RumCommand& command, const CoreContext& context, const EventWriter& writer
) {
  // Compute TTFD duration: time from process launch to the moment of this call.
  const Timestamp& now = command.base.issued_at;
  const Timestamp& launch = context.process_launch_time;
  const double duration_ns = static_cast<double>((now - launch).count());

  static constexpr double kMaxTTFDNs = 90.0 * 1'000'000'000.0;
  if (duration_ns <= 0.0 || duration_ns >= kMaxTTFDNs) {
    _deps.get().diagnostic_logger.Warning(
        "Ignoring TTFD; computed value falls outside expected range",
        {{"ttfd_ns", duration_ns}}
    );
    return;
  }

  if (!_ttid_has_fired) {
    // TTID has not yet been recorded. Store the raw duration and command base
    // (for the attribute snapshot) and defer emission until
    // ReportAppDisplayInitialized fires, at which point both events will be
    // written together with any necessary clamping applied.
    _pending_ttfd = PendingTTFD{duration_ns, command.base};
    return;
  }

  // TTID has already fired: emit immediately, clamping so TTFD >= TTID.
  // This handles the timestamp-capture race: if TTFD's issued_at was sampled on
  // the calling thread before TTID's but enqueued after it, the raw TTFD duration
  // may be less than TTID. Apply max() to match iOS/Android behavior.
  const double clamped_duration_ns = std::max(duration_ns, _ttid_duration_ns);
  const auto view_opt = GetActiveView();
  const RumViewScope* view = view_opt ? &view_opt->get() : nullptr;
  SendAppLaunchVitalEvent(
      command.base,
      view,
      clamped_duration_ns,
      RumVitalAppLaunchMetric::TTFD,
      "time_to_full_display",
      context,
      writer
  );
}

void RumSessionScope::SendAppLaunchVitalEvent(
    const RumCommandParams& base,
    const RumViewScope* view,
    double duration_ns,
    RumVitalAppLaunchMetric metric,
    std::string_view vital_name,
    const CoreContext& context,
    const EventWriter& writer
) {
  const RumScopeDependencies& deps = _deps;

  const UUID vital_id = UUID::Random();
  RumVitalAppLaunchEvent ev(
      context.process_launch_time,
      deps.application_id,
      _session_id,
      RumSessionType::User,
      view ? view->GetViewID() : UUID::Zero,
      view ? view->GetKey() : "",
      vital_id,
      vital_name,
      RumVitalAppLaunchType::AppLaunch,
      metric,
      duration_ns
  );

  // Set optional view name
  if (view && !view->GetName().empty()) {
    ev.view.name.value = view->GetName();
  }

  // Set 'context' to the full set of user-specified attributes, merging:
  // global <- view <- command
  Attribute merged_attributes = Attribute::Object();
  AttributeMerge::AssembleObject(
      merged_attributes,
      {base.global_attributes,
       view ? view->GetAttributes() : Attribute::Object(),
       base.attributes}
  );
  if (merged_attributes.GetObjectPropertyCount() > 0) {
    ev.context.value = merged_attributes;
  }

  // Enrich with OS and device properties from CoreContext
  RumEventEnrichment::PopulateCommonProperties(context, ev);

  // Serialize and write
  std::string_view json = deps.EncodeEvent(ev);
  const bool bypass_tracking_consent = false;
  writer(Block{json.data(), json.size()}, Block{}, bypass_tracking_consent);
}

void RumSessionScope::AttemptViewTransfer(
    const RumCommand& command,
    const RumSessionScope::ViewDetails& prev_view,
    const CoreContext& context,
    const EventWriter& writer
) {
  // If this session is the initial session, or if it's not sampled, then
  // RumApplicationScope should not have provided it with any last_active_view details
  // on construction
  DATADOG_ASSERT(!_is_initial_session, "attempted view transfer in initial session");
  DATADOG_ASSERT(_is_sampled, "attempted view transfer in non-sampled session");

  // If we've ever created any foreground views in this session, old views from the
  // previous session are now irrelevant and should be ignored
  if (_num_views_opened > 0) {
    return;
  }

  // If we currently have an active view of any kind, we should forget the old view
  if (GetActiveView()) {
    return;
  }

  // Open a new view scope in this session, giving it a new UUID and start time, but
  // inheriting the view key, name, and attributes from the previous view
  const bool is_initial_view = false;
  const UUID view_id = UUID::Random();
  std::string_view view_key = prev_view.key;
  std::string_view view_name = prev_view.name;
  const Timestamp start_time = command.base.issued_at;
  _view_scopes.Push(
      _deps, *this, is_initial_view, view_id, view_key, view_name, start_time
  );
  _num_views_opened++;

  // Dispatch a StartView command to the newly-created view to ensure that it's properly
  // initialized, transferring the previous view's custom attributes in the process
  RumCommandParams base = command.base;
  base.attributes = prev_view.attributes;
  const RumCommand cmd = RumCommand::StartView(std::move(base), view_key, view_name);
  _view_scopes.Propagate(cmd, context, writer);
}

void RumSessionScope::StoreLastActiveViewEvent(RumViewEvent ev) {
  // Move the provided value into our std::optional<RumViewEvent> member
  _last_active_view_event = std::move(ev);
}

std::optional<RumViewEvent> RumSessionScope::ConsumeLastActiveViewEvent() {
  // Move the value out of our std::optional<RumViewEvent> member, clearing that member
  // in the process
  std::optional<RumViewEvent> result =
      std::exchange(_last_active_view_event, std::nullopt);
  return result;
}

}  // namespace datadog::impl
