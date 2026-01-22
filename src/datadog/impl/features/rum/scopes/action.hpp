// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include "datadog/impl/features/rum/scope.hpp"

namespace datadog::impl {

/**
 * Node in the RUM scope tree modeling an 'Action', which represents a user-initiated
 * interaction with the app.
 *
 * All actions are tracked within the context of their parent View, and a View may only
 * have one active action at a time.
 *
 * == SESSION LIFECYCLE ==
 *
 * If a session is explicitly stopped, any active action will be stopped and allowed to
 * send a RUM 'action' event.
 *
 * If a session expires due to inactivity timeout or excessive duration, any active
 * action will be discarded without sending an event. If a session refresh occurs
 * thereafter, the original action remains lost: even if the original view state is
 * recreated in the new session, the new view is created with no actions.
 *
 * == VIEW LIFECYCLE ==
 *
 * If a view is stopped while an action is active, that action will be stopped and
 * allowed to send a RUM 'action' event.
 *
 * == ACTION LIFECYCLE ==
 *
 * Once a RUM Action scope is opened, it will send only one RUM 'action' event, and that
 * event will be sent when the action is stopped.
 *
 * Actions created via AddAction are 'discrete', whereas actions created via StartAction
 * are 'continuous.' We expect a StopAction call to signal the end of a continuous
 * action; we do not expect a StopAction call for a discrete action. All Actions have a
 *
 * See `RumViewScope` for more details.
 */
class RumActionScope {
 public:
  explicit RumActionScope(
      const RumScopeDependencies& deps,
      class RumViewScope& parent,
      const UUID& action_id,
      RumActionType type,
      std::string_view name,
      Timestamp start_time,
      Duration timeout_duration,
      const Attribute& attributes
  );

  // Scopes are non-copyable but movable
  ~RumActionScope() = default;
  RumActionScope(const RumActionScope&) = delete;
  RumActionScope& operator=(const RumActionScope&) = delete;
  RumActionScope(RumActionScope&&) = default;
  RumActionScope& operator=(RumActionScope&&) = default;

  // RumContextProvider interface
  void PopulateContext(struct RumContext& out_context) const;

  // RumScope interface
  RumScopeResult Process(const RumCommand& command);

 private:
  /**
   * Generates and sends a RUM action event in response to the given command.
   */
  void SendActionEvent(const RumCommand& command, const Timestamp& completed_at);

 private:
  std::reference_wrapper<const RumScopeDependencies> _deps;
  std::reference_wrapper<class RumViewScope> _parent;

  UUID _action_id;
  RumActionType _type;
  std::string _name;
  Timestamp _started_at;
  Timestamp _expires_at;
  Attribute _attributes;

  int32_t _num_active_resources{0};
  size_t _num_resources_recorded{0};
  size_t _num_errors_recorded{0};

  bool _has_sent_action_event{false};

 public:
  UUID GetActionID() const { return _action_id; }
  bool HasSentActionEvent() const { return _has_sent_action_event; }
};

}  // namespace datadog::impl
