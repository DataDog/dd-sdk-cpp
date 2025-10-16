// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cinttypes>
#include <string_view>

#include "datadog/uuid.hpp"

namespace datadog::impl {

/**
 * Additional context for the RUM feature that's made accessible to other features via
 * CoreContext.
 *
 * Other features can read the current RUM feature context in order to access relevant
 * state like the current session ID. This allows those other features to enrich their
 * event payloads with RUM data, which facilitates correlation in the backend.
 */
struct RumFeatureContext {
  UUID application_id;  // UUID::Zero if RUM not initialized
  UUID session_id;      // UUID::Zero if no active session
  UUID view_id;         // UUID::Zero if no active view
  UUID action_id;       // UUID::Zero if no active action
};

/**
 * Indicates why and how a new RUM session was created, tracking the lifecycle
 * transition from the previous session or initial app state. You might also call this
 * the session's "start reason."
 *
 * TODO: This definition is based on the schema for RUM events that's maintained in the
 * rum-events-format repo. Other SDKs have tooling to generate code from these JSON
 * schemas; in this codebase we currently maintain those types by hand.
 */
enum class RumSessionPrecondition : uint8_t {
  /**
   * Session started because the user launched the instrumented application in the
   * foreground (normal interactive launch).
   */
  UserAppLaunch,
  /**
   * Session started automatically after the previous session timed out from lack of
   * user input.
   */
  InactivityTimeout,
  /**
   * Session started automatically after the previous session reached its maximum
   * allowed duration.
   */
  MaxDuration,
  /**
   * Session started because the app was launched in a background state.
   */
  BackgroundLaunch,
  /**
   * Session started because the app was launched by the operating system, without user
   * input, as an optimization.
   */
  Prewarm,
  /**
   * Session started following a previous non-interactive session.
   */
  FromNonInteractiveSession,
  /**
   * Session started after the previous session was explicitly stopped via a
   * StopSession() API call.
   */
  ExplicitStop
};

std::string_view RumSessionPrecondition_ToString(RumSessionPrecondition value);

}  // namespace datadog::impl
