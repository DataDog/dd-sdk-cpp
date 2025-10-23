// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "features/rum/scopes/application.hpp"

#include <utility>

#include "assert.hpp"
#include "core/context.hpp"
#include "datadog/rum.hpp"
#include "datadog/uuid.hpp"
#include "features/rum/context.hpp"

namespace datadog::impl {

RumApplicationScope::RumApplicationScope(const RumScopeDependencies& deps)
    : _deps(deps) {}

void RumApplicationScope::PopulateContext(RumContext& out_context) const {
  const RumScopeDependencies& deps = _deps;
  out_context.application_id = deps.application_id;
}

RumScopeResult RumApplicationScope::Process(const RumCommand& command) {
  // On SDK init, create an initial session
  if (command.Is<RumSDKInitPayload>()) {
    // Open a brand new session scope
    DATADOG_ASSERT(!_active_session, "Received SDKInit with valid session");
    DATADOG_ASSERT(!_prev_session, "Received SDKInit with valid previous session");
    _active_session = CreateSession(command);
    DATADOG_ASSERT(_active_session, "Failed to create initial session on SDKInit");

    // TODO(RUM-12243): If the app was launched in the foreground, issue an
    // ApplicationStart command so that the initial session will create an initial
    // 'ApplicationLaunch' view. If the app was _not_ launched in the foreground, defer
    // the ApplicationStart command until the next command is processed.

    // No need to propagate SDKInit to the new session scope
    return RumScopeResult::RemainOpen;
  }

  // If this command requires an active session and we don't have one, attempt to create
  // one, effectively "refreshing" the session that was most recently active
  if (command.HasFlag(RumCommandFlags::RequiresActiveSession) && !_active_session) {
    AttemptSessionRefresh(command);
  }

  // If the previous session was explicitly closed while a view was still active, and
  // we've received a StopView call targeting that view, clear the cached state so that
  // we won't recreate the stopped view if we end up refreshing the session
  if (!_active_session && _prev_session) {
    if (command.Is<RumStopViewPayload>()) {
      if (const auto& prev_view = _prev_session->GetActiveViewOnClose()) {
        if (prev_view->key == command.As<RumStopViewPayload>().key) {
          _prev_session->ClearActiveViewOnClose();
        }
      }
    }
  }

  // If we have an active session scope, propagate the command to it
  if (_active_session) {
    // Allow the session to process the command, and potentially propagate it to child
    // views etc.
    const RumScopeResult session_result = _active_session->Process(command);

    // If the session scope was closed in response to the command, update our state and
    // refresh the session if necessary
    if (session_result == RumScopeResult::Close) {
      // If closed, the session should have an end reason set
      if (!_active_session->GetEndReason()) {
        // If it failed to set one, drop it entirely and bail out rather than proceeding
        // with a malformed _prev_session
        DATADOG_ASSERT(false, "RumSessionScope is active (no end reason) after close");
        _active_session.reset();
        return RumScopeResult::RemainOpen;
      }

      // The session is no longer active: move it to _prev_session so we can retain any
      // state we might need when transitioning to the next session
      _prev_session = std::move(_active_session);
      _active_session.reset();

      // Initiate our session refresh logic: if we can and should create a new session
      // to succeed the one that just closed, this will set _active_session; otherwise
      // it will leave us without an active session
      AttemptSessionRefresh(command);

      // If we've ended up with a new session, propagate the original command to it
      if (_active_session) {
        const RumScopeResult result = _active_session->Process(command);

        // If we've just created a new session to handle a command, that command should
        // _not_ close the session: if it does, drop the session as if it never existed
        if (result == RumScopeResult::Close) {
          DATADOG_ASSERT(
              false,
              "command that triggered session refresh caused the new session to "
              "immediately close"
          );
          _active_session.reset();
        }
      }
    }
  }

  // The Application scope is tied to the lifetime of the process; it's never closed
  return RumScopeResult::RemainOpen;
}

RumSessionScope RumApplicationScope::CreateSession(
    const RumCommand& command, const RumSessionScope* prev_session
) {
  // Establish basic session details
  const bool is_initial_session = prev_session == nullptr;
  const UUID session_id = UUID::Random();

  // Make a new, independent sampling decision for each new session
  const RumScopeDependencies& deps = _deps;
  const bool is_sampled = deps.ShouldSampleSession();

  // Determine the proper "session precondition" value, which indicates the reason
  // the session was started
  RumSessionPrecondition precondition{RumSessionPrecondition::UserAppLaunch};
  if (!prev_session) {
    // If this is the initial session: just assume 'UserAppLaunch'
    // TODO(RUM-12245): Use 'BackgroundLaunch' if config indicates that app was launched
    // in the background
    precondition = RumSessionPrecondition::UserAppLaunch;
  } else {
    // This is not the initial session: figure out why the previous session ended
    RumSessionScope::EndReason end_reason = RumSessionScope::EndReason::Stopped;
    const auto end_reason_opt = prev_session->GetEndReason();
    DATADOG_ASSERT(end_reason_opt, "previous session has no end reason");
    if (end_reason_opt) {
      end_reason = *end_reason_opt;
    }

    // And initialize our start precondition based on that end reason
    switch (end_reason) {
      case RumSessionScope::EndReason::TimedOutDueToInactivity:
        precondition = RumSessionPrecondition::InactivityTimeout;
        break;
      case RumSessionScope::EndReason::ExceededMaxDuration:
        precondition = RumSessionPrecondition::MaxDuration;
        break;
      case RumSessionScope::EndReason::Stopped:
        precondition = RumSessionPrecondition::ExplicitStop;
        break;
    }
  }

  // If this new session is succeeding a previous session, and it's sampled, convey the
  // details of its last active view (if any) so that the new session can recreate it if
  // needed
  std::optional<RumSessionScope::ViewDetails> last_active_view;
  if (prev_session && is_sampled) {
    // TODO(RUM-12245): Views are only transferred if the app is in the foreground
    // TODO(RUM-12246): Views are only transferred if the app is in the foreground
    last_active_view = prev_session->GetActiveViewOnClose();
  }

  // Open a new RumSessionScope with these details
  const Timestamp start_time = command.base.issued_at;
  return RumSessionScope(
      deps,
      *this,
      is_initial_session,
      is_sampled,
      session_id,
      precondition,
      start_time,
      last_active_view
  );
}

void RumApplicationScope::AttemptSessionRefresh(const RumCommand& command) {
  // This function is only called when no active session exists
  DATADOG_ASSERT(!_active_session, "AttemptSessionRefresh called with active session");

  // A StopSession command can never trigger the creation of a new session
  if (command.Is<RumStopSessionPayload>()) {
    return;
  }

  // Resolve the end reason of the previous session
  if (!_prev_session) {
    DATADOG_ASSERT(false, "AttemptSessionRefresh called with no previous session");
    return;
  }
  auto end_reason_opt = _prev_session->GetEndReason();
  if (!end_reason_opt) {
    DATADOG_ASSERT(false, "unable to refresh session with no end reason");
    return;
  }
  const RumSessionScope::EndReason end_reason = *end_reason_opt;

  // If the application has explicitly requested that we stop tracking, we want to be
  // more conservative and only create a new session in response to an
  // explicitly-tracked user interaction which opens a new view or action scope
  if (end_reason == RumSessionScope::EndReason::Stopped) {
    const bool should_reopen_session_after_explicit_stop =
        command.Is<RumStartActionPayload>() || command.Is<RumAddActionPayload>() ||
        command.Is<RumStartViewPayload>();
    if (!should_reopen_session_after_explicit_stop) {
      return;
    }
  }

  // Create a new session to succeed the previous one, effectively refreshing it, and
  // potentially conveying the details of the last-active view in case the new session
  // need to recreate that view's state
  const RumSessionScope& prev_session = *_prev_session;
  _active_session = CreateSession(command, &prev_session);

  // The previous session no longer needs to exist: all its resources and actions are
  // simply lost when the session stops
  _prev_session.reset();
}

ScopeRef<const RumSessionScope> RumApplicationScope::GetActiveSession() const {
  return _active_session;
}

ScopeRef<const RumSessionScope> RumApplicationScope::GetMostRecentSession() const {
  if (_active_session) {
    return _active_session;
  }
  return _prev_session;
}

}  // namespace datadog::impl
