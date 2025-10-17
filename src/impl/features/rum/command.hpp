// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cinttypes>
#include <variant>

#include "attribute/typed_attribute.hpp"
#include "platform/clock.hpp"

namespace datadog::impl {

/**
 * Parameters that are defined for all RUM commands.
 */
struct RumCommandParams {
  platform::Timestamp issued_at;
  ObjectAttribute global_attributes;
  ObjectAttribute attributes;
  bool is_user_interaction;

  explicit RumCommandParams(
      platform::Timestamp in_issued_at,
      ObjectAttribute in_global_attributes,
      ObjectAttribute in_attributes
  )
      : issued_at(in_issued_at),
        global_attributes(in_global_attributes),
        attributes(in_attributes) {}
};

/**
 * On SDKInit, the SDK has been initialized with RUM as a registered feature.
 */
struct RumSDKInitPayload {
  static constexpr const char* COMMAND_NAME = "SDKInit";

  static constexpr bool is_user_interaction = false;
  static constexpr bool should_create_new_session_after_explicit_stop = false;
};

/**
 * On StopSession, the application has explicitly signalled the end of the current
 * session via a StopSession API call.
 */
struct RumStopSessionPayload {
  static constexpr const char* COMMAND_NAME = "StopSession";

  static constexpr bool is_user_interaction = false;
  static constexpr bool should_create_new_session_after_explicit_stop = false;
};

/**
 * 'UserInteraction' is a no-op command type that we can use as a stand-in when testing
 * command-processing logic that responds to any user interactions.
 */
struct RumUserInteractionPayload {
  static constexpr const char* COMMAND_NAME = "UserInteraction";

  static constexpr bool is_user_interaction = true;
  static constexpr bool should_create_new_session_after_explicit_stop = true;
};

struct RumCommand {
  using Payload =
      std::variant<RumSDKInitPayload, RumStopSessionPayload, RumUserInteractionPayload>;

  RumCommandParams base;
  Payload payload;

  explicit RumCommand(RumCommandParams&& in_base, Payload&& in_payload)
      : base(std::move(in_base)), payload(std::move(in_payload)) {}

  /** Creates a new 'SDKInit' command. */
  static RumCommand SDKInit(RumCommandParams&& base) {
    return RumCommand(std::move(base), RumSDKInitPayload());
  }

  /** Creates a new 'StopSession' command. */
  static RumCommand StopSession(RumCommandParams&& base) {
    return RumCommand(std::move(base), RumStopSessionPayload());
  }

  /** Creates a new 'UserInteraction' command. */
  static RumCommand UserInteraction(RumCommandParams&& base) {
    return RumCommand(std::move(base), RumUserInteractionPayload());
  }

  /**
   * Returns true if this command represents a user interaction.
   *
   * User interactions keep a session alive: if a session remains open for a certain
   * duration (e.g. 15m) without receiving any user interactions, any command processed
   * after that threshold will result in a new session being created, with the old
   * session being closed due to inactivity timeout.
   *
   * This flag is true for command types that are instigated by an intentional action on
   * the part of the end user: e.g. navigating to a View, providing some input that
   * results in an Action being tracked.
   */
  bool IsUserInteraction() const {
    // This fact is intrinsic to the command type: resolve the 'is_user_interaction'
    // flag defined for the command's payload type
    return std::visit(
        [](const auto& payload) {
          using T = std::decay_t<decltype(payload)>;
          return T::is_user_interaction;
        },
        payload
    );
  }

  /**
   * Determines what should happen in a case where there is no active session to handle
   * this command because the previous session was explicitly stopped via a StopSession
   * API call.
   *
   * Returns true if we should automatically create a new session to handle this
   * command.
   *
   * This flag is generally true for all command types, except for 'StopSession' itself
   * (as StopSession calls should be idempotent) and commands representing lifecycle
   * events that are wholly internal to the RUM implementation.
   */
  bool ShouldCreateNewSessionAfterExplicitStop() const {
    // Resolve intrinsic type param 'should_create_new_session_after_explicit_stop'
    return std::visit(
        [](const auto& payload) {
          using T = std::decay_t<decltype(payload)>;
          return T::should_create_new_session_after_explicit_stop;
        },
        payload
    );
  }
};

}  // namespace datadog::impl
