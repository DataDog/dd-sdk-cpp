// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cinttypes>
#include <functional>
#include <optional>

#include "core/feature_types/rum.hpp"
#include "datadog/uuid.hpp"
#include "features/rum/scope.hpp"
#include "features/rum/scopes/view.hpp"
#include "platform/clock.hpp"

namespace datadog::impl {

/**
 * Node in the RUM scope tree modeling a 'Session', which represents a contiguous series
 * of user interactions. The session scope is responsible for creating and managing the
 * lifetime of child RumViewScopes in response to RUM API calls, and for propagating
 * events to the relevant views.
 *
 * == SESSION LIFECYCLE ==
 *
 * See `RumApplicationScope` for a top-down description of the RUM Session lifecycle.
 *
 * Key details of the session lifecycle as it concerns `RumSessionScope`:
 *
 * - The decision to close a session is made by the session itself, in response to
 *   a `RumCommand`.
 *   - See `RumSessionScope::ShouldCloseRatherThanProcessing()`.
 *
 * - When a session scope is closed, it MUST also set an `EndReason`. If the session has
 *   a valid end reason, then it is no longer active. There is no distinction:
 *   - If a session is active, its scope is open.
 *   - If a session is no longer active, its scope is closed.
 *
 * - Whether the closing of the session is propagated to child views depends on the
 *   session's end reason:
 *   - If a session expires, its views are _NOT_ notified and they do _NOT_ get the
 *     chance to send events to the backend recording their final state.
 *   - If a session is explicitly stopped, we _DO_ propagate the `StopSession` command
 *     to child views and allow them to send final view events.
 *
 * == VIEW LIFECYCLE ==
 *
 * See `RumApplicationScope` for a top-down description of session refresh and
 * view transfer.
 *
 * Key details of the RUM View lifecycle as it concerns `RumSessionScope`:
 *
 * - When a session is closed for any reason: if the session has an active view, the
 *   essential details of that view are stored in `_active_view_on_close`.
 *   - See `RumSessionScope::OnClose()`.
 *
 * - If `RumApplicationScope` handles a `StopView` command for that view thereafter, it
 *   will force `_active_view_on_close` to be cleared.
 *   - See `RumApplicationScope::Process()`.
 *
 * - When the `RumApplicationScope` creates a new session, if the previous session has
 *   `_active_view_on_close` details cached, _and_ the new session is sampled, those
 *   details will be conveyed to the new session scope and stored as
 *   `_active_view_from_predecessor`.
 *   - See `RumApplicationScope::CreateSession()`.
 *
 * - When the new session processes a command that `RequiresActiveView`, it may create
 *   a new view if:
 *   - `_active_view_from_predecessor` details are cached, and
 *   - There is no active view, and
 *   - No views have ever been created in the scope of the new session, and
 *   - No StopView commands have been processed for that view.
 *   - See `RumSessionScope::AttemptViewTransfer()`.
 */
class RumSessionScope {
 public:
  static const platform::Duration INACTIVITY_TIMEOUT_DURATION;
  static const platform::Duration MAX_SESSION_DURATION;

  /**
   * Reason that a session has ended.
   */
  enum class EndReason : uint8_t {
    /** The session received no user interaction for the configured timeout duration. */
    TimedOutDueToInactivity,
    /** The duration of the session exceeded the configured maximum. */
    ExceededMaxDuration,
    /** The session was explicitly stopped via a StopSession() API call. */
    Stopped
  };

  /**
   * Essential details of a RUM view scope that was previously active. Used to
   * facilitate view transfer, i.e. the recreation of the last-active view after session
   * refresh.
   */
  struct ViewDetails {
    std::string key;
    std::string name;
    Attribute attributes;

    explicit ViewDetails(
        std::string_view in_key,
        std::string_view in_name,
        const Attribute& in_attributes
    )
        : key(in_key), name(in_name), attributes(in_attributes) {}
  };

 public:
  explicit RumSessionScope(
      const RumScopeDependencies& deps,
      class RumApplicationScope& parent,
      bool is_initial_session,
      bool is_sampled,
      const UUID& session_id,
      RumSessionPrecondition start_precondition,
      platform::Timestamp start_time,
      const std::optional<ViewDetails>& active_view_from_predecessor
  );

 public:
  // Scopes are non-copyable but movable
  ~RumSessionScope() = default;
  RumSessionScope(const RumSessionScope&) = delete;
  RumSessionScope& operator=(const RumSessionScope&) = delete;
  RumSessionScope(RumSessionScope&&) = default;
  RumSessionScope& operator=(RumSessionScope&&) = default;

  // RumContextProvider interface
  void PopulateContext(struct RumContext& out_context) const;

  // RumScope interface
  RumScopeResult Process(const RumCommand& command);

 private:
  /**
   * Given a command, returns a valid end reason if the session should close in response
   * to that command rather than processing it. Returns std::nullopt if session should
   * remain open and process the command.
   */
  std::optional<EndReason> ShouldCloseRatherThanProcessing(
      const RumCommand& command
  ) const;

  /**
   * Updates internal state when the session scope is about to be closed. The provided
   * `command` will _not_ be propagated to child scopes via the normal code path in
   * `Process()`, so `OnClose()` has a chance to do so explicitly if desired.
   */
  void OnClose(const RumCommand& command, EndReason end_reason);

  /**
   * Attempts to create a new active view scope that's initialized from the details of
   * a previous view. The new view will have a unique UUID, and its start time will
   * correspond with the timestamp of the given command, but its essential details will
   * be preserved from the previous scope.
   */
  void AttemptViewTransfer(const RumCommand& command, const ViewDetails& prev_view);

 private:
  std::reference_wrapper<const RumScopeDependencies> _deps;
  std::reference_wrapper<class RumApplicationScope> _parent;

  // Essential session state
  bool _is_initial_session;
  bool _is_sampled;
  UUID _session_id;
  RumSessionPrecondition _precondition;
  platform::Timestamp _started_at;
  platform::Timestamp _last_interaction_at;
  std::optional<EndReason> _end_reason;

  // View details for session/view lifecycle transitions: if the previous session had an
  // active view when it was closed, it will be stored here so we can recreate it if
  // necessary in response to a command that requires an active view.
  std::optional<ViewDetails> _active_view_from_predecessor;
  // When _we_ are closed, the details of any active view are stored here so they can
  // be conveyed to the next session.
  std::optional<ViewDetails> _active_view_on_close;

  size_t _num_views_opened{0};
  ScopeArray<RumViewScope> _view_scopes;

 public:
  inline bool IsInitialSession() const { return _is_initial_session; }
  inline bool IsSampled() const { return _is_sampled; }
  inline UUID GetSessionID() const { return _session_id; }
  inline RumSessionPrecondition GetStartReason() const { return _precondition; }
  inline std::optional<EndReason> GetEndReason() const { return _end_reason; }

  std::optional<ViewDetails> GetActiveViewOnClose() const {
    return _active_view_on_close;
  }
  void ClearActiveViewOnClose() { _active_view_on_close.reset(); }

  ScopeRef<const RumViewScope> GetActiveView() const;
};

}  // namespace datadog::impl
