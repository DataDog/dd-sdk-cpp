// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "features/rum/scopes/session.hpp"

#include "assert.hpp"
#include "features/rum/context.hpp"
#include "features/rum/scopes/application.hpp"

namespace datadog::impl {

const platform::Duration RumSessionScope::INACTIVITY_TIMEOUT_DURATION =
    std::chrono::minutes(15);
const platform::Duration RumSessionScope::MAX_SESSION_DURATION = std::chrono::hours(4);

RumSessionScope::RumSessionScope(
    const RumScopeDependencies& deps,
    class RumApplicationScope& parent,
    bool is_initial_session,
    bool is_sampled,
    const UUID& session_id,
    RumSessionPrecondition start_precondition,
    platform::Timestamp start_time
)
    : _deps(deps),
      _parent(parent),
      _is_initial_session(is_initial_session),
      _is_sampled(is_sampled),
      _session_id(session_id),
      _precondition(start_precondition),
      _started_at(start_time),
      _last_interaction_at(start_time) {}

RumSessionScope RumSessionScope::CreateSuccessorFor(
    const RumSessionScope& prev_session, platform::Timestamp start_time
) {
  // Establish basic details of our session scope
  const bool is_initial_session = false;
  const UUID session_id = UUID::Random();

  // Determine why the last session ended: if the session scope is being treated as no
  // longer active, it must have a valid end reason
  EndReason end_reason = EndReason::Stopped;
  const auto end_reason_opt = prev_session.GetEndReason();
  DATADOG_ASSERT(end_reason_opt, "previous session has no end reason");
  if (end_reason_opt) {
    end_reason = *end_reason_opt;
  }

  // Determine the start reason for our new session based on that end reason
  RumSessionPrecondition precondition = RumSessionPrecondition::ExplicitStop;
  switch (end_reason) {
    case EndReason::TimedOutDueToInactivity:
      precondition = RumSessionPrecondition::InactivityTimeout;
      break;
    case EndReason::ExceededMaxDuration:
      precondition = RumSessionPrecondition::MaxDuration;
      break;
    case EndReason::Stopped:
      precondition = RumSessionPrecondition::ExplicitStop;
      break;
  }

  // Make a new, independent sampling decision for this new session
  const RumScopeDependencies& deps = prev_session._deps;
  const bool is_sampled = deps.ShouldSampleSession();

  // Create a new session scope
  RumSessionScope session(
      deps,
      prev_session._parent,
      is_initial_session,
      is_sampled,
      session_id,
      precondition,
      start_time
  );

  // TODO(RUM-11368): In the case of timeout/duration, transfer active views from the
  // expired session, preserving relevant view state (stable identifier, path name,
  // etc.) while creating a new RumViewScope with a new start timestamp and UUID for
  // each view. In the case of explicit stop, transfer only the last-active view, unless
  // the command being processed is StartView, in which case no views should be retained

  // Return our fully initialized session scope
  return session;
}

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

  // If it's been more than (e.g.) 15 minutes since the last user interaction, end this
  // session due to inactivity and go no further
  const platform::Timestamp& now = command.base.issued_at;
  const platform::Duration elapsed_since_last_interaction = now - _last_interaction_at;
  if (elapsed_since_last_interaction >= INACTIVITY_TIMEOUT_DURATION) {
    _end_reason = EndReason::TimedOutDueToInactivity;
    return RumScopeResult::Close;
  }

  // If it's been more than (e.g.) 4 hours since the session started, end this session
  // and go no further
  const platform::Duration elapsed_since_start = now - _started_at;
  if (elapsed_since_start > MAX_SESSION_DURATION) {
    _end_reason = EndReason::ExceededMaxDuration;
    return RumScopeResult::Close;
  }

  // If the command is 'StopSession', the user explicitly requested that we end the
  // session via a StopSession API call
  if (std::holds_alternative<RumStopSessionPayload>(command.payload)) {
    _end_reason = EndReason::Stopped;
    return RumScopeResult::Close;
  }

  // -- Update session state (irrespective of sampling decision)

  // If the command was dispatched in response to a user interaction, refresh our
  // last-interaction timestamp to reset the clock on the inactivity timeout
  if (command.base.is_user_interaction) {
    _last_interaction_at = command.base.issued_at;
  }

  // -- Early-out if session not sampled

  // For sessions that are excluded from sampling, we don't bother keeping track of
  // state for views etc. - since no events will be stored or uploaded in conjunction
  // with this session, there's no need to do any further command processing
  if (!_is_sampled) {
    return RumScopeResult::RemainOpen;
  }

  // TODO(RUM-11368): Handle view creation, propagate events to child views
  return RumScopeResult::RemainOpen;
}

}  // namespace datadog::impl
