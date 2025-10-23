// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cinttypes>
#include <string_view>
#include <variant>

#include "attribute/typed_attribute.hpp"
#include "platform/clock.hpp"

namespace datadog::impl {

/**
 * Flags describing the intrinsic traits of each type of RumCommandPayload that can be
 * processed by RUM scopes.
 */
enum class RumCommandFlags : uint8_t {
  /**
   * This command is subject to the ordinary command-processing rules. It has no special
   * impact on session or view lifecycle.
   */
  None = 0,
  /**
   * In order for this command to be processed, the application must have an active
   * session. If set, the application scope may trigger session refresh when processing
   * this command.
   */
  RequiresActiveSession = 0x01,
  /**
   * In order for this command to be processed, the application must have an active
   * session with an active view. If set, the session scope may attempt to automatically
   * recreate the last active view when processing this command.
   */
  RequiresActiveView = RequiresActiveSession | 0x02,
  /**
   * This command represents any change in application state that results from
   * user-initiated input, such as navigation between views or tracked actions.
   */
  UserInteraction = RequiresActiveSession | 0x04,
};

/**
 * Enables bitwise OR on command flags.
 */
constexpr RumCommandFlags operator|(RumCommandFlags lhs, RumCommandFlags rhs) {
  return static_cast<RumCommandFlags>(
      static_cast<std::underlying_type_t<RumCommandFlags>>(lhs) |
      static_cast<std::underlying_type_t<RumCommandFlags>>(rhs)
  );
}

/**
 * Enables bitwise AND on command flags.
 */
constexpr RumCommandFlags operator&(RumCommandFlags lhs, RumCommandFlags rhs) {
  return static_cast<RumCommandFlags>(
      static_cast<std::underlying_type_t<RumCommandFlags>>(lhs) &
      static_cast<std::underlying_type_t<RumCommandFlags>>(rhs)
  );
}

/**
 * Parameters that are defined for all RUM commands.
 */
struct RumCommandParams {
  Timestamp issued_at;
  Attribute global_attributes;
  Attribute attributes;

  explicit RumCommandParams(
      Timestamp in_issued_at, Attribute in_global_attributes, Attribute in_attributes
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
  static constexpr RumCommandFlags FLAGS = RumCommandFlags::None;
};

/**
 * On ApplicationStart, either a.) the SDK has been initialized in response to a
 * user-initiated foreground launch of the application, or b.) another RUM command has
 * been processed by the first session prior to the explicit creation of the first view.
 * This results in the creation of an initial 'ApplicationLaunch' view.
 */
// TODO(RUM-12242): Add 'ApplicationStart'

/**
 * On StopSession, the application has explicitly signalled the end of the current
 * session via a StopSession API call.
 */
struct RumStopSessionPayload {
  static constexpr const char* COMMAND_NAME = "StopSession";
  static constexpr RumCommandFlags FLAGS = RumCommandFlags::None;
};

/**
 * On StartView, the application has called the StartView API function, explicitly
 * recording that the user has just been presented with a new view.
 */
struct RumStartViewPayload {
  static constexpr const char* COMMAND_NAME = "StartView";
  static constexpr RumCommandFlags FLAGS = RumCommandFlags::UserInteraction;

  std::string_view key;   // Unique identifier; e.g. '/accounts', 'user-info'
  std::string_view name;  // Human-readable name to identify this view in the RUM UI

  explicit RumStartViewPayload(std::string_view in_key, std::string_view in_name)
      : key(in_key), name(in_name.empty() ? in_key : in_name) {}
};

/**
 * On StopView, the application has called the StopView API function, explicitly
 * recording that the view with the given key is no longer active.
 */
struct RumStopViewPayload {
  static constexpr const char* COMMAND_NAME = "StopView";
  static constexpr RumCommandFlags FLAGS = RumCommandFlags::None;

  std::string_view key;

  explicit RumStopViewPayload(std::string_view in_key) : key(in_key) {}
};

/**
 * TEMP: This command type is not yet supported; it's defined for use in session/view
 * lifecycle tests.
 *
 * TODO(RUM-12202): Fully implement resource commands
 */
struct RumStartResourcePayload {
  static constexpr const char* COMMAND_NAME = "StartResource";
  static constexpr RumCommandFlags FLAGS = RumCommandFlags::RequiresActiveView;
};

/**
 * TEMP: This command type is not yet supported; it's defined for use in session/view
 * lifecycle tests.
 *
 * TODO(RUM-12202): Fully implement resource commands
 */
struct RumStopResourcePayload {
  static constexpr const char* COMMAND_NAME = "StopResource";
  static constexpr RumCommandFlags FLAGS = RumCommandFlags::None;
};

/**
 * TEMP: This command type is not yet supported; it's defined for use in session/view
 * lifecycle tests.
 *
 * TODO(RUM-11369): Fully implement action commands
 */
struct RumAddActionPayload {
  static constexpr const char* COMMAND_NAME = "AddAction";
  static constexpr RumCommandFlags FLAGS =
      RumCommandFlags::UserInteraction | RumCommandFlags::RequiresActiveView;
};

/**
 * TEMP: This command type is not yet supported; it's defined for use in session/view
 * lifecycle tests.
 *
 * TODO(RUM-11369): Fully implement action commands
 */
struct RumStartActionPayload {
  static constexpr const char* COMMAND_NAME = "StartAction";
  static constexpr RumCommandFlags FLAGS =
      RumCommandFlags::UserInteraction | RumCommandFlags::RequiresActiveView;
};

/**
 * TEMP: This command type is not yet supported; it's defined for use in session/view
 * lifecycle tests.
 *
 * TODO(RUM-11369): Fully implement action commands
 */
struct RumStopActionPayload {
  static constexpr const char* COMMAND_NAME = "StopAction";
  static constexpr RumCommandFlags FLAGS = RumCommandFlags::UserInteraction;
};

struct RumCommand {
  using Payload = std::variant<
      RumSDKInitPayload,
      // TODO(RUM-12242): Add 'ApplicationStart'
      RumStopSessionPayload,
      RumStartViewPayload,
      RumStopViewPayload,
      RumStartResourcePayload,
      RumStopResourcePayload,
      RumAddActionPayload,
      RumStartActionPayload,
      RumStopActionPayload>;

  RumCommandParams base;
  Payload payload;

  explicit RumCommand(RumCommandParams&& in_base, Payload&& in_payload)
      : base(std::move(in_base)), payload(std::move(in_payload)) {}

  /** Creates a new 'SDKInit' command. */
  static RumCommand SDKInit(RumCommandParams&& base) {
    return RumCommand(std::move(base), RumSDKInitPayload());
  }

  // TODO(RUM-12242): Add 'ApplicationStart'

  /** Creates a new 'StopSession' command. */
  static RumCommand StopSession(RumCommandParams&& base) {
    return RumCommand(std::move(base), RumStopSessionPayload());
  }

  /** Creates a new 'StartView' command. */
  static RumCommand StartView(
      RumCommandParams&& base, std::string_view key, std::string_view name
  ) {
    return RumCommand(std::move(base), RumStartViewPayload(key, name));
  }

  /** Creates a new 'StopView' command. */
  static RumCommand StopView(RumCommandParams&& base, std::string_view key) {
    return RumCommand(std::move(base), RumStopViewPayload(key));
  }

  /** Creates a new 'StartResource' command. */
  static RumCommand StartResource(RumCommandParams&& base) {
    return RumCommand(std::move(base), RumStartResourcePayload());
  }

  /** Creates a new 'StopResource' command. */
  static RumCommand StopResource(RumCommandParams&& base) {
    return RumCommand(std::move(base), RumStopResourcePayload());
  }

  /** Creates a new 'AddAction' command. */
  static RumCommand AddAction(RumCommandParams&& base) {
    return RumCommand(std::move(base), RumAddActionPayload());
  }

  /** Creates a new 'StartAction' command. */
  static RumCommand StartAction(RumCommandParams&& base) {
    return RumCommand(std::move(base), RumStartActionPayload());
  }

  /** Creates a new 'StopAction' command. */
  static RumCommand StopAction(RumCommandParams&& base) {
    return RumCommand(std::move(base), RumStopActionPayload());
  }

  /**
   * Returns true if the command's payload type matches the given type T. If true, it is
   * safe to access the payload by calling As<T>().
   */
  template <typename T>
  bool Is() const {
    return std::holds_alternative<T>(payload);
  }

  /**
   * Returns a reference to the payload of a command given its known type. Calling
   * As<T>() when the command's type is not known to be `T` is undefined behavior.
   */
  template <typename T>
  const T& As() const {
    return std::get<T>(payload);
  }

  /**
   * Returns whether the given flag is set for commands of this type.
   */
  bool HasFlag(RumCommandFlags flag) const {
    // Resolve the static FLAGS value declared on the payload type for this command
    const RumCommandFlags type_flags = std::visit(
        [](const auto& payload) {
          using T = std::decay_t<decltype(payload)>;
          return T::FLAGS;
        },
        payload
    );

    // Test whether that value has the desired (potentially multi-bit) flag set
    return (type_flags & flag) == flag;
  }
};

}  // namespace datadog::impl
