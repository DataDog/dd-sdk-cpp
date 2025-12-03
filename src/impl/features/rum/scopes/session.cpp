// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "features/rum/scopes/session.hpp"

#include <algorithm>

#include "assert.hpp"
#include "features/rum/context.hpp"
#include "features/rum/scopes/application.hpp"

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

RumScopeResult RumSessionScope::Process(const RumCommand& command) {
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
    OnClose(command, *_end_reason);
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
    AttemptViewTransfer(command, *_active_view_from_predecessor);

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

  // TODO(RUM-12242): In case of off-view commands, create ApplicationLaunch view if
  // warranted

  // TODO(RUM-11247): In case of off-view commands, create Background view if warranted

  // Propagate the command to any and all child view scopes
  _view_scopes.Propagate(command);

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

void RumSessionScope::OnClose(const RumCommand& command, EndReason end_reason) {
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

  // If this session was explicitly stopped, propagate the StopSession command to child
  // views so they can be cleanly finalized and so that final view events can be sent:
  // note that we _don't_ send final view events after session expiration
  if (end_reason == EndReason::Stopped) {
    DATADOG_ASSERT(command.Is<RumStopSessionPayload>(), "stopped by non-StopSession");
    _view_scopes.Propagate(command);
  }
}

void RumSessionScope::AttemptViewTransfer(
    const RumCommand& command, const RumSessionScope::ViewDetails& prev_view
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
  _view_scopes.Propagate(cmd);
}

}  // namespace datadog::impl
