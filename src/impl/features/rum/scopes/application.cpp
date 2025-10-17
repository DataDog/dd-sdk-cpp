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
  if (std::holds_alternative<RumSDKInitPayload>(command.payload)) {
    // Open a brand new session scope
    DATADOG_ASSERT(!_active_session, "Received SDKInit with valid session");
    DATADOG_ASSERT(!_prev_session, "Received SDKInit with valid previous session");
    _active_session = CreateInitialSession(command);

    // TODO(RUM-11368): Start 'ApplicationLaunch' view, but only in case of foreground
    // launch?

    // No need to propagate SDKInit to the new session scope
    return RumScopeResult::RemainOpen;
  }

  // If we have no active session to process the command, create a new one, unless the
  // command type is one that should be ignored in this case
  if (!_active_session && command.ShouldCreateNewSessionAfterExplicitStop()) {
    // We'll generally always have an active session: we create one initially on
    // SDKInit, and if the current session times out due to inactivity or excessive
    // duration, we _immediately_ create a new session to succeed it. The only exception
    // is an explicit StopSession() API call, which will leave the application without
    // an active session until the next command is received. So if _active_session is
    // null, we should have a previous session that was explicitly stopped.
    if (!_prev_session) {
      // If this invariant doesn't hold, silently drop all commands in release builds
      DATADOG_ASSERT(
          false,
          "received non-SDKInit command with no active session and no previous session"
      );
      return RumScopeResult::RemainOpen;
    }

    // Verify our expectation: this code path should only be reachable when the previous
    // session was explicitly stopped
    DATADOG_ASSERT(
        _prev_session->GetEndReason().has_value(),
        "on command with no active session, previous session has no end reason"
    );
    DATADOG_ASSERT(
        _prev_session->GetEndReason().value() == RumSessionScope::EndReason::Stopped,
        "on command with no active session, previous session has unexpected end reason"
    );

    // Create a new session to succeed the previous one
    _active_session =
        RumSessionScope::CreateSuccessorFor(*_prev_session, command.base.issued_at);
  }

  // If we have an active session scope, propagate the command to it
  if (_active_session) {
    // Allow the session to process the command, and potentially propagate it to child
    // views etc.
    const RumScopeResult session_result = _active_session->Process(command);

    // If the session scope was closed in response to the command, update our state and
    // refresh the session if necessary
    if (session_result == RumScopeResult::Close) {
      // If closed, the session should no longer be active - i.e. it should have an end
      // reason set
      auto end_reason_opt = _active_session->GetEndReason();
      if (!end_reason_opt) {
        // Refrain from setting _prev_session to a RumSessionScope that doesn't have a
        // valid EndReason: just drop the newly-closed session and abort
        DATADOG_ASSERT(false, "RumSessionScope active (no end reason) after close");
        _active_session.reset();
        return RumScopeResult::RemainOpen;
      }
      const RumSessionScope::EndReason end_reason = *end_reason_opt;

      // The session is no longer active: move it to _prev_session so we can retain any
      // state we might need when creating the next session
      _prev_session = std::move(_active_session);
      _active_session.reset();

      // Determine whether we need to create a new session to succeed the one that was
      // just stopped
      bool needs_successor = false;
      switch (end_reason) {
        case RumSessionScope::EndReason::TimedOutDueToInactivity:
        case RumSessionScope::EndReason::ExceededMaxDuration:
          // If the session was closed due to inactivity timeout or because it got too
          // long in the tooth, we'll "refresh" it by creating a new session to succeed
          // it, then handle the command in the scope of that new session
          needs_successor = true;
          break;

        case RumSessionScope::EndReason::Stopped:
          // If the session was explicitly stopped, leave it alone; we'll create a new
          // session on the next user interaction
          needs_successor = false;
          break;
      }

      // If we need to refresh, create another session scope to replace the one we just
      // closed, inheriting its views and other persistent state
      if (needs_successor) {
        _active_session =
            RumSessionScope::CreateSuccessorFor(*_prev_session, command.base.issued_at);

        // Process the command in the newly-created session scope, since our previous
        // session (the one that closed) didn't handle it
        const RumScopeResult successor_result = _active_session->Process(command);

        // Our new session scope should remain open, since it's just now been created,
        // and since the command we're giving it is _not_ StopSession: if that invariant
        // is violated, just destroy the new session and forget it ever existed
        if (successor_result == RumScopeResult::Close) {
          DATADOG_ASSERT(
              false, "successor session returned Close after processing first command"
          );
          _active_session.reset();
        }
      }
    }
  }

  // The Application scope is tied to the lifetime of the process; it's never closed
  return RumScopeResult::RemainOpen;
}

RumSessionScope RumApplicationScope::CreateInitialSession(const RumCommand& command) {
  // Basic session details
  const bool is_initial_session = true;
  const UUID session_id = UUID::Random();

  // Determine if the session should be sampled: if so, we'll populate the full set of
  // state for it, and we'll generate RUM events and send them to intake for this
  // session. If not sampled, we'll record that the session exists locally, but no views
  // will be populated, no events will be generated, no context will be published, etc.
  const RumScopeDependencies& deps = _deps;
  const bool is_sampled = deps.ShouldSampleSession();

  // We currently assume that all application launches are user-initiated
  // TODO: Support detection/configuration of background launches?
  const RumSessionPrecondition precondition = RumSessionPrecondition::UserAppLaunch;

  // Create and return a brand new session
  const platform::Timestamp start_time = command.base.issued_at;
  return RumSessionScope(
      deps, *this, is_initial_session, is_sampled, session_id, precondition, start_time
  );
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
