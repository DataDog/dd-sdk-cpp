// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cinttypes>
#include <string>

#include "datadog/uuid.hpp"

#include "datadog/impl/types/rum.hpp"

namespace datadog::impl {

/**
 * `RumContext` is a point-in-time snapshot describing the high-level state of the
 * application as tracked by RUM.
 *
 * These values encapsulate the state required to correlate events of different types
 * (View, Action, Error, etc.) as belonging to the same application, session, or view.
 * A context is attached to every outgoing RUM event so that the Datadog backend can
 * make these correlations.
 *
 * Not to be confused with `RumFeatureContext`, which provides other features within the
 * SDK with the state required to enrich their events with RUM context. By contrast,
 * `RumContext` is used internally, within the `RumScope` hierarchy, so that scopes
 * lower in the hierarchy have all the information they need to build a complete RUM
 * event.
 */
struct RumContext {
  /**
   * The static RUM Application ID configured for this SDK instance; serialized as
   * `event.application.id`.
   */
  UUID application_id;
  /**
   * ID of the most-recently-active RUM session, or UUID::Zero if no session has yet
   * been started; serialized as `event.session.id`.
   */
  UUID session_id;
  /**
   * Whether the session indicated by session_id (if nonzero) is sampled. If a session
   * is not sampled, we refrain from publishing session_id, view_id, etc. in the global
   * context: there's no point in enriching events with RUM context for sessions that
   * the backend won't know anything about.
   */
  bool session_is_sampled{false};
  /**
   * Whether the session indicated by session_id (if nonzero) is still active. When a
   * session is created, it is active initially. If a session ends for any reason, its
   * session_id will be retained, with this flag set to true, until the start of the
   * next session.
   */
  bool session_is_active{false};
  /**
   * If session_id is nonzero, indicates the reason that the session was started.
   */
  RumSessionPrecondition session_precondition{RumSessionPrecondition::UserAppLaunch};

  /**
   * ID of the currently-active RUM view scope, or UUID::Zero if no view is active.
   * Populates `view.id` in events.
   */
  UUID active_view_id;
  /**
   * Key associated with the active view, if active_view_id is nonzero. The 'key' value
   * is provided by the user in StartView/StopView API calls, and it uniquely identifies
   * the view in the context of the application. Also known by 'path' or 'url' elsewhere
   * in the platform. Populates `view.url` in events.
   */
  std::string active_view_key;
  /**
   * Human-readable name associated with the active view, if active_view_id is nonzero.
   * Matches the 'name' value provided by the user in StartView API calls. If no name
   * was provided, matches the view's 'key' value. Populates `view.name` in events.
   */
  std::string active_view_name;

  /**
   * ID of the currently-active RUM action scope, or UUID::Zero if no action is active.
   */
  UUID active_action_id;

 public:
  /**
   * Resets all values held by this struct to zero.
   */
  void Reset();

  /**
   * Converts this internal snapshot of our RUM state to an SDK-facing context struct.
   */
  RumFeatureContext ToFeatureContext() const;
};

/**
 * `RumContextProvider` describes any type that defines the following member function:
 *
 * - void PopulateContext(RumContext& out_context) const;
 *
 * Each scope in the RUM hierarchy (e.g. Application -> Session -> View) implements
 * RumContextProvider. The scope is immutable at each level but is progressively
 * enriched in a bottom-up fashion. Calling `RumApplicationScope::PopulateContext()`
 * will populate only application-level details, while calling the same function on
 * `RumViewScope` will yield a fully-enriched context with application, session, and
 * view details.
 *
 * In C++20, this interface could be expressed as:
 *
 * template <typename T>
 * concept RumContextProvider = requires(const T& provider, RumContext& ctx) {
 *   { provider.PopulateContext(ctx) } -> std::same_as<void>;
 * }
 */
struct RumContextProvider {};

}  // namespace datadog::impl
