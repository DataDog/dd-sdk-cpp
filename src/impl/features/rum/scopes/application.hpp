// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <optional>
#include <random>
#include <string>

#include "features/rum/scope.hpp"
#include "features/rum/scopes/session.hpp"

namespace datadog::impl {

/**
 * Top-level node in the tree of RUM scopes that model the current state of the user's
 * interactions with the application.
 *
 * The RumApplicationScope represents the application as a whole, and as such it is
 * always present while RUM is enabled. The application scope is responsible for
 * creating and managing the lifetime of child RumSessionScopes.
 *
 * == SESSION LIFECYCLE ==
 *
 * Key details of the RUM Session lifecycle as it concerns `RumApplicationScope`:
 *
 * - Sessions are relevant to billing, so these rules are important to get right.
 *
 * - The session lifecycle plays out through the processing of commands.
 *   - We don't proactively poll the status of a session and evaluate session lifecycle
 *     rules in the background. When any session scope is opened or closed, there is a
 *     `RumCommand` that precipitates that change.
 *
 * - If RUM is enabled, an initial session is always created unconditionally.
 *   - When the `Rum` feature is initialized, it dispatches an `SDKInit` command before
 *     allowing any other commands to be processed.
 *   - `RumApplicationScope::Process` handles `SDKInit` by creating an initial session.
 *
 * - When any session is created, its session scope is "open", and the session is
 *   therefore "active" from the start.
 *
 * - When a session scope is closed (as a result of `RumSessionScope::Process` returning
 *   `RumScopeResult::Close`), then the session is no longer active.
 *
 * - A session scope can be closed for two reasons:
 *   - The session can "expire", which can happen in either of two situations:
 *     - The session went 15 minutes without receiving any user interaction.
 *     - The session lasted more than 4 hours in total.
 *   - The session can be "explicitly stopped", which happens when:
 *     - The application calls the `StopSession()` API function.
 *   - (These rules are encoded in `RumSessionScope::ShouldCloseRatherThanProcessing`.)
 *
 * - These reasons for closing a session are treated differently:
 *   - When a session expires, we attempt to continue seamlessly in response to any user
 *     interaction command, creating a new session and carrying over some of its state.
 *   - When a session is explicitly stopped, we leave the application with no active
 *     session unless the application calls `StartView()`, `StartAction()`, or
 *     `AddAction()` thereafter.
 *
 * - The application has 0 or 1 active sessions at any given time:
 *   - If the last session was explicitly stopped via `StopSession()`, there may be no
 *     active session.
 *   - In all other cases, there is always an active session.
 *
 * - When we create a new session to succeed a previously-closed session (whether
 *   expired or explicitly stopped), we "refresh" the original session: this process is
 *   called "session refresh".
 *   - `RumCommand` types may be flagged `RequiresActiveSession`. If such a command is
 *     processed while there is no active session, we will attempt a session refresh.
 *   - Session refresh may or may not complete, depending on the command type and the
 *     reason the previous session scope was closed.
 *
 * == VIEW LIFECYCLE ==
 *
 * Key details of RUM View lifecycle as it concerns `RumApplicationScope`:
 *
 * - When a session is active, any calls to `CreateView()` will result in a new view
 *   scope opening (and becoming the active view) in the scope of that session.
 *
 * - When transitioning from one session scope to the next, we may convey some state to
 *   the new session to facilitate "view transfer".
 *   - If Session A has an active view and we are creating Session B to succeed it, we
 *     may cache the details of Session A's last-active view in Session B.
 *   - `RumCommand` types may be flagged `RequiresActiveView`. If Session B processes
 *     such a command while it has no active view, it may attempt to recreate the view
 *     that was active in Session A at the time of its closing.
 *   - View transfer is conditional, and will not occur if:
 *     - The new session is not sampled
 *     - The previous session had no active view at the time of its closing
 *     - Any of the following happens after the session is created:
 *       - A `StopView` call is issued for the last-active view
 *       - Any `StartView` call is issued
 *   - For exact details of view transfer implementation, see:
 *     - `RumSessionScope::OnClose()` -> caches last-active view state in Session A
 *     - `RumApplicationScope::CreateSession()` -> conveys that state to Session B
 *     - `RumSessionScope::AttemptViewTransfer()` -> uses that state if applicable
 *
 * For more details on view lifecycle, see `RumSessionScope`.
 */
class RumApplicationScope {
 public:
  explicit RumApplicationScope(const RumScopeDependencies& deps);

  // RumContextProvider interface
  void PopulateContext(struct RumContext& out_context) const;

  // RumScope interface
  RumScopeResult Process(const RumCommand& command);

 private:
  /**
   * Creates and returns a new session scope. If prev_session is null, the session will
   * be treated as the first session in the application; otherwise the newly-created
   * session will succeed prev_session, potentially inheriting its last active view.
   */
  RumSessionScope CreateSession(
      const RumCommand& command, const RumSessionScope* prev_session = nullptr
  );

  /**
   * Given a command that has no `_active_session` to handle it, attempts to create a
   * new session to succeed `_prev_session`.
   *
   * If successful, populates `_active_session` with the newly-created session scope.
   * Does not propagate `command` to the new session.
   */
  void AttemptSessionRefresh(const RumCommand& command);

 private:
  // Non-owning reference to the immutable set of input values injected into all scopes
  std::reference_wrapper<const RumScopeDependencies> _deps;

  // Child sessions
  std::optional<RumSessionScope> _active_session;
  std::optional<RumSessionScope> _prev_session;

 public:
  ScopeRef<const RumSessionScope> GetActiveSession() const;
  ScopeRef<const RumSessionScope> GetMostRecentSession() const;
};

}  // namespace datadog::impl
