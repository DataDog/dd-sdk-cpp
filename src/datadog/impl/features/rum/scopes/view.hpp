// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <optional>
#include <string>

#include "datadog/uuid.hpp"

#include "datadog/impl/attribute/typed_attribute.hpp"
#include "datadog/impl/core/context.hpp"
#include "datadog/impl/core/feature_scope.hpp"
#include "datadog/impl/features/rum/containers/resource_map.hpp"
#include "datadog/impl/features/rum/scope.hpp"
#include "datadog/impl/features/rum/scopes/action.hpp"
#include "datadog/impl/platform/clock.hpp"

namespace datadog::impl {

/**
 * Node in the RUM scope tree modeling a 'View', which represents some user-facing
 * portion of the client application (e.g. a page, a screen, a level, etc.) that the
 * user is interacting with, in the context of a parent RUM Session.
 *
 * Both user interactions (in the form of RUM Actions) and network requests (in the form
 * of RUM Resources) are tracked as child scopes of the View.
 *
 * == VIEW LIFECYCLE ===
 *
 * See `RumSessionScope` and `RumApplicationScope` for more details on session refresh
 * and view transfer.
 *
 * Key details of the RUM View lifecycle as it concerns `RumViewScope`:
 *
 * - A view's `key` is the stable identifier used to identify the same view across
 *   multiple view scopes.
 *
 * - When a view scope is opened, the view is considered active initially.
 *
 * - A view scope may be rendered inactive when a view lifecycle command is processed:
 *   - Any `StartView` command (beyond the first, which initializes the view scope)
 *     indicates that the session has a new active view.
 *   - A `StopView` command (with a `key` matching this view scope) explicitly ends this
 *     view.
 *
 * - Whether is a view is *active* is subtly different from whether that view's scope is
 *   open. Once a view is stopped, `is_active` is false and it will not respond to
 *   further view lifecycle commands, but whether it remains open depends on the state
 *   of any child resources:
 *   - If any resource scopes remain open, the view scope remains open as well (so that
 *     `StopResource` can be propagated on completion of the network request) despite
 *     no longer being active.
 *   - Once an inactive view has no pending resources, it will be closed.
 *
 * == ACTION LIFECYCLE ==
 *
 * A RUM Action represents some instantaneous or ongoing user interaction in the context
 * of the current view. The active View may have 0 or 1 active Actions at any given
 * time.
 *
 * `RumViewScope` handles new actions differently depending upon the API function used
 * to create them:
 *
 * - A `StartAction` call records a "continuous" action.
 * - An `AddAction` call records a "discrete" action.
 * - An `AddAction` call with `RumActionType::Custom` records a custom discrete action,
 *   which is handled according to special-case rules.
 *
 * Custom discrete actions are always processed immediately, regardless of whether
 * there's an active action scope of another type.
 *
 * All other action scopes persist for a finite duration, and only one such scope may be
 * open at any given time. If the application calls `AddAction` (with a type other than
 * `custom`) or `StartAction` while a user action is already active in the current view,
 * that action will be ignored.
 *
 * All action scopes besides custom discrete actions have a finite, nonzero lifetime.
 * A discrete action has an expected lifetime of 100ms, while a continuous action's
 * lifetime is limited to 10 seconds. If no `StopAction` call is made, the action will
 * be automatically stopped when the next command is processed after that expected
 * lifetime has passed, unless the lifetime of the action has been extended by one or
 * more active RUM Resources.
 *
 * While the active view has an active action, any newly-recorded RUM Resources or RUM
 * Errors will be associated with that action. For example, if the user clicks a button,
 * resulting in an `AddAction` call at T+0, the app may initiate an HTTP request
 * thereafter, resulting in a `StartResource` call at T+10ms. In this case, the Action
 * will remain active until the next `StopResource` call is made, even if that occurs
 * after T+100ms.
 *
 * Calling `StopAction` will always end the active action, regardless of its type, name,
 * expected lifetime, current age, or number of active resources.
 *
 * If a view is stopped via StartView, StopView, or StopSession, any active action will
 * receive the event and be stopped as well. Upon session expiration, any active action
 * within the active view is lost.
 *
 * See `RumActionScope` for more details on action lifecycle.
 *
 * TODO(RUM-12202): Clarify as needed once resources are implemented
 *
 * == VIEW ATTRIBUTES ==
 *
 * A RUM View can have custom attributes assigned to it. All RUM events produced in the
 * context of a view scope will include these attributes in a `context` property, merged
 * from various sources, e.g.:
 *
 * - `view`:     Global RUM attributes <- View attributes
 * - `resource`: Global RUM attributes <- View attributes <- Resource attributes
 * - `action`:   Global RUM attributes <- View attributes <- Action attributes
 *
 * Global attributes are set via `Rum::AddAttribute` (or `dd_rum_add_attribute`). These
 * values are stored outside of the `RumViewScope` and are propagated through the scope
 * tree, carried by each `RumCommand` that's processed.
 *
 * View-level attributes may be initialized on `Rum::StartView`, modified thereafter via
 * `Rum::AddViewAttribute` and `Rum::RemoveViewAttribute`, and finalized on
 * `Rum::StopView`. These values are stored in `RumViewScope`, in `_view_attributes`.
 * An additional value, `_global_and_view_attributes`, stores the union of global and
 * view attributes, populated on an as-needed basis.
 */
class RumViewScope {
 public:
  explicit RumViewScope(
      const RumScopeDependencies& deps,
      class RumSessionScope& parent,
      bool is_initial_view,
      const UUID& view_id,
      std::string_view key,
      std::string_view name,
      Timestamp start_time
  );

  // Scopes are non-copyable but movable
  ~RumViewScope() = default;
  RumViewScope(const RumViewScope&) = delete;
  RumViewScope& operator=(const RumViewScope&) = delete;
  RumViewScope(RumViewScope&&) = default;
  RumViewScope& operator=(RumViewScope&&) = default;

  // RumContextProvider interface
  void PopulateContext(struct RumContext& out_context) const;

  // RumScope interface
  RumScopeResult Process(
      const RumCommand& command, const CoreContext& context, const EventWriter& writer
  );

 private:
  /**
   * Indicates whether we should generate an event to describe the updated state of the
   * view, and if so, whether that should be a full `view` event or an incremental
   * `view_update` event (NYI).
   */
  enum class ViewEventType : uint8_t { None, Full };

  /**
   * Updates view state in response to the given command, then returns what sort of view
   * event (if any) we should generate in response.
   */
  ViewEventType HandleCommand(
      const RumCommand& command, const CoreContext& context, const EventWriter& writer
  );

  // Handler functions for individual command types
  ViewEventType HandleStopSession(const RumCommandParams& base);
  ViewEventType HandleStartView(
      const RumCommandParams& base, const RumStartViewPayload& payload
  );
  ViewEventType HandleStopView(
      const RumCommandParams& base, const RumStopViewPayload& payload
  );
  ViewEventType HandleAddViewAttribute(
      const RumCommandParams& base, const RumAddViewAttributePayload& payload
  );
  ViewEventType HandleRemoveViewAttribute(
      const RumCommandParams& base, const RumRemoveViewAttributePayload& payload
  );
  ViewEventType HandleAddAction(
      const RumCommandParams& base,
      const RumAddActionPayload& payload,
      const CoreContext& context,
      const EventWriter& writer
  );
  ViewEventType HandleStartAction(
      const RumCommandParams& base, const RumStartActionPayload& payload
  );
  ViewEventType HandleStartResource(
      const RumCommandParams& base, const RumStartResourcePayload& payload
  );
  ViewEventType HandleAddError(
      const RumCommandParams& base,
      const RumAddErrorPayload& payload,
      const CoreContext& context,
      const EventWriter& writer
  );

  /**
   * Renders the view inactive, updating all necessary state to finalize the scope. This
   * includes updating timestamps and freezing stored attribute values so that all
   * remaining events generated in the context of this view will reflect the set of
   * attribute values as of the time this function was called.
   *
   * @param accept_command_attributes Indicates whether base.attributes should be merged
   *  into the final set of attributes. This is true when the command that rendered this
   *  view inactive explicitly targeted this scope, as in the case of StopView.
   */
  void BecomeInactive(const RumCommandParams& base, bool accept_command_attributes);

  void ProcessDiscreteCustomAction(
      const RumCommandParams& base,
      std::string_view name,
      const CoreContext& context,
      const EventWriter& writer
  );

  void OpenActionScope(
      const RumCommandParams& base,
      RumActionType type,
      std::string_view name,
      bool is_continuous
  );

  void LogDroppedAction(RumActionType type, std::string_view name) const;

  static std::string_view IdentifyTargetResourceKey(const RumCommand& command);

  /**
   * Generates and sends a RUM view event in response to the given command.
   */
  void SendViewEvent(
      const RumCommand& command, const CoreContext& context, const EventWriter& writer
  );

  /**
   * Generates and sends a RUM error event in response to the given command.
   */
  void SendErrorEvent(
      const RumCommandParams& base,
      const RumAddErrorPayload& payload,
      const CoreContext& context,
      const EventWriter& writer
  );

 private:
  std::reference_wrapper<const RumScopeDependencies> _deps;
  std::reference_wrapper<class RumSessionScope> _parent;

  bool _is_initial_view;  // Whether this is the very first View created by the SDK
  UUID _view_id;          // UUID generated anew for each view scope
  std::string _key;       // The 'key' value passed to StartView, a.k.a. 'path' or 'url'
  std::string _name;      // The 'name' value passed to StartView

  Timestamp _started_at;
  Timestamp _rendered_inactive_at;

  /**
   * User-specified attributes associated with this view. Contains the actual set of
   * values that explicitly describe this view scope.
   */
  ObjectAttribute _view_attributes;

  /**
   * User-specified attributes to be included in events generated by this view. Contains
   * the union of both global and view attributes, with view attributes taking
   * precendence in case of name conflicts.
   *
   * While the view is active, we merge the latest set of attribute values into this
   * object immediately before generating events.
   *
   * Once the view becomes inactive, we merge attributes values into this object one
   * final time, then refrain from updating it any further.
   */
  ObjectAttribute _global_and_view_attributes;

  bool _is_active{true};                  // Whether this is currently the active view
  bool _has_processed_start_view{false};  // If true, any 'StartView' command will flag
                                          // this scope as inactive

  /**
   * Total nmber of RUM 'view' events sent during the lifetime of this view scope.
   */
  uint64_t _num_view_events_sent{0};
  /**
   * Number of actions (of any type) that have been completed within the scope of this
   * view and have sent a RUM 'action' event.
   */
  uint64_t _num_actions_completed{0};
  uint64_t _num_errors_reported{0};
  uint64_t _num_resources_completed{0};

  std::optional<RumActionScope> _active_action_scope;

  RumResourceMap _resource_scopes;

 public:
  bool IsActive() const { return _is_active; }
  UUID GetViewID() const { return _view_id; }
  std::string_view GetKey() const { return _key; }
  std::string_view GetName() const { return _name; }
  Attribute GetAttributes() const { return _view_attributes.attribute; }
  Timestamp GetStartedAt() const { return _started_at; }

  ScopeRef<const RumActionScope> GetActiveAction() const;
  const RumSessionScope& GetParentSessionScope() const { return _parent; }
};

}  // namespace datadog::impl
