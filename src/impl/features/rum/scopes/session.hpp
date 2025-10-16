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
#include "platform/clock.hpp"

namespace datadog::impl {

/**
 * Node in the RUM scope tree modeling a 'Session', which represents a contiguous series
 * of user interactions. The session scope is responsible for creating and managing the
 * lifetime of child RumViewScopes in response to RUM API calls, and for propagating
 * events to the relevant views.
 */
class RumSessionScope {
 public:
  static const platform::Duration INACTIVITY_TIMEOUT_DURATION;
  static const platform::Duration MAX_SESSION_DURATION;

  enum class EndReason : uint8_t {
    /** The session received no user interaction for the configured timeout duration. */
    TimedOutDueToInactivity,
    /** The duration of the session exceeded the configured maximum. */
    ExceededMaxDuration,
    /** The session was explicitly stopped via a StopSession() API call. */
    Stopped
  };

 public:
  explicit RumSessionScope(
      const RumScopeDependencies& deps,
      class RumApplicationScope& parent,
      bool is_initial_session,
      bool is_sampled,
      const UUID& session_id,
      RumSessionPrecondition start_precondition,
      platform::Timestamp start_time
  );

  static RumSessionScope CreateSuccessorFor(
      const RumSessionScope& prev_session, platform::Timestamp start_time
  );

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
  std::reference_wrapper<const RumScopeDependencies> _deps;
  std::reference_wrapper<class RumApplicationScope> _parent;

  bool _is_initial_session;
  bool _is_sampled;
  UUID _session_id;
  RumSessionPrecondition _precondition;
  platform::Timestamp _started_at;
  platform::Timestamp _last_interaction_at;
  std::optional<EndReason> _end_reason;

  // TODO(RUM-11368): ScopeArray<RumViewScope> _view_scopes;

 public:
  inline bool IsInitialSession() const { return _is_initial_session; }
  inline UUID GetSessionID() const { return _session_id; }
  inline RumSessionPrecondition GetStartReason() const { return _precondition; }
  inline std::optional<EndReason> GetEndReason() const { return _end_reason; }
};

}  // namespace datadog::impl
