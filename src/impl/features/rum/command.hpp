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
        attributes(in_attributes),
        is_user_interaction(false) {}
};

/**
 * On SDKInit, the SDK has been initialized with RUM as a registered feature.
 */
struct RumSDKInitPayload {
  static constexpr const char* COMMAND_NAME = "SDKInit";
};

/**
 * On StopSession, the application has explicitly signalled the end of the current
 * session via a StopSession API call.
 */
struct RumStopSessionPayload {
  static constexpr const char* COMMAND_NAME = "StopSession";
};

/**
 * 'UserInteraction' is a no-op command type that we can use as a stand-in when testing
 * command-processing logic that responds to any user interactions.
 */
struct RumUserInteractionPayload {
  static constexpr const char* COMMAND_NAME = "UserInteraction";
};

struct RumCommand {
  using Payload =
      std::variant<RumSDKInitPayload, RumStopSessionPayload, RumUserInteractionPayload>;

  RumCommandParams base;
  Payload payload;

  explicit RumCommand(RumCommandParams&& in_base, Payload&& in_payload)
      : base(std::move(in_base)), payload(std::move(in_payload)) {}

  /** Creates a new 'SDKInit' command. */
  static RumCommand SDKInit(RumCommandParams&& in_base) {
    return RumCommand(std::move(in_base), RumSDKInitPayload());
  }

  /** Creates a new 'StopSession' command. */
  static RumCommand StopSession(RumCommandParams&& in_base) {
    return RumCommand(std::move(in_base), RumStopSessionPayload());
  }

  /** Creates a new 'UserInteraction' command. */
  static RumCommand UserInteraction(RumCommandParams&& in_base) {
    in_base.is_user_interaction = true;
    return RumCommand(std::move(in_base), RumUserInteractionPayload());
  }
};

}  // namespace datadog::impl
