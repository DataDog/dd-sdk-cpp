// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cinttypes>
#include <optional>
#include <string_view>
#include <variant>

#include "datadog/rum.hpp"

#include "datadog/impl/attribute/typed_attribute.hpp"
#include "datadog/impl/features/rum/resource_types.hpp"
#include "datadog/impl/platform/clock.hpp"

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
 * On AddViewAttribute, the application has called the AddViewAttribute API function,
 * inserting or updating a custom view-level attribute on the current view.
 */
struct RumAddViewAttributePayload {
  static constexpr const char* COMMAND_NAME = "AddViewAttribute";
  static constexpr RumCommandFlags FLAGS = RumCommandFlags::None;

  std::string_view name;
  Attribute value;

  explicit RumAddViewAttributePayload(
      std::string_view in_name, const Attribute& in_value
  )
      : name(in_name), value(in_value) {}
};

/**
 * On RemoveViewAttribute, the application has called the RemoveViewAttribute API
 * function, removing a custom view-level attribute from the current view.
 */
struct RumRemoveViewAttributePayload {
  static constexpr const char* COMMAND_NAME = "RemoveViewAttribute";
  static constexpr RumCommandFlags FLAGS = RumCommandFlags::None;

  std::string_view name;

  explicit RumRemoveViewAttributePayload(std::string_view in_name) : name(in_name) {}
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
 * On StartResource, the application has called the StartResource API function,
 * recording the start of an HTTP request.
 */
struct RumStartResourcePayload {
  static constexpr const char* COMMAND_NAME = "StartResource";
  static constexpr RumCommandFlags FLAGS = RumCommandFlags::RequiresActiveView;

  // TODO(RUM-13165): Store string values as std::string in all commands; once commands
  // are processed in another thread (RUM-13164), these types will no longer be safe to
  // use as members of any payload type
  std::string_view key;
  RumRequestDetails request;

  explicit RumStartResourcePayload(
      std::string_view in_key, const RumRequestDetails& in_request
  )
      : key(in_key), request(in_request) {}
};

/**
 * On StopResource, the application has called the StopResource or StopResourceWithError
 * API functions, concluding an HTTP request that was previously recorded via
 * StartResource.
 */
struct RumStopResourcePayload {
  static constexpr const char* COMMAND_NAME = "StopResource";
  static constexpr RumCommandFlags FLAGS = RumCommandFlags::None;

  std::string_view key;
  RumResponseDetails response;
  std::optional<RumErrorDetails> error;

  explicit RumStopResourcePayload(
      std::string_view in_key,
      const RumResponseDetails& in_response,
      const std::optional<RumErrorDetails>& in_error
  )
      : key(in_key), response(in_response), error(in_error) {}
};

/**
 * On AddAction, the application has called the AddAction API function, recording an
 * instantaneous or short-lived user interaction of a specific type.
 */
struct RumAddActionPayload {
  static constexpr const char* COMMAND_NAME = "AddAction";
  static constexpr RumCommandFlags FLAGS =
      RumCommandFlags::UserInteraction | RumCommandFlags::RequiresActiveView;

  RumActionType type;
  std::string_view name;

  explicit RumAddActionPayload(RumActionType in_type, std::string_view in_name)
      : type(in_type), name(in_name) {}
};

/**
 * On StartAction, the application has called the StartAction API function, recording
 * the start of a continuous user interaction of a specific type.
 */
struct RumStartActionPayload {
  static constexpr const char* COMMAND_NAME = "StartAction";
  static constexpr RumCommandFlags FLAGS =
      RumCommandFlags::UserInteraction | RumCommandFlags::RequiresActiveView;

  RumActionType type;
  std::string_view name;

  explicit RumStartActionPayload(RumActionType in_type, std::string_view in_name)
      : type(in_type), name(in_name) {}
};

/**
 * On StopAction, the application has called the StopAction API function, recording that
 * the currently-active action has stopped.
 */
struct RumStopActionPayload {
  static constexpr const char* COMMAND_NAME = "StopAction";
  static constexpr RumCommandFlags FLAGS = RumCommandFlags::UserInteraction;

  std::string_view name;

  explicit RumStopActionPayload(std::string_view in_name) : name(in_name) {}
};

/**
 * On AddError, the application has called the AddError API function, recording that the
 * application has encountered a runtime error that should be reported in the context of
 * the current view.
 */
struct RumAddErrorPayload {
  static constexpr const char* COMMAND_NAME = "AddError";
  static constexpr RumCommandFlags FLAGS = RumCommandFlags::RequiresActiveView;

  RumErrorSource source;
  RumErrorDetails error;

  explicit RumAddErrorPayload(RumErrorSource in_source, const RumErrorDetails& in_error)
      : source(in_source), error(in_error) {}
};

struct RumCommand {
  using Payload = std::variant<
      RumSDKInitPayload,
      // TODO(RUM-12242): Add 'ApplicationStart'
      RumStopSessionPayload,
      RumStartViewPayload,
      RumAddViewAttributePayload,
      RumRemoveViewAttributePayload,
      RumStopViewPayload,
      RumStartResourcePayload,
      RumStopResourcePayload,
      RumAddActionPayload,
      RumStartActionPayload,
      RumStopActionPayload,
      RumAddErrorPayload>;

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

  /** Creates a new 'AddViewAttribute' command. */
  static RumCommand AddViewAttribute(
      RumCommandParams&& base, std::string_view name, const Attribute& value
  ) {
    return RumCommand(std::move(base), RumAddViewAttributePayload(name, value));
  }

  /** Creates a new 'RemoveViewAttribute' command. */
  static RumCommand RemoveViewAttribute(
      RumCommandParams&& base, std::string_view name
  ) {
    return RumCommand(std::move(base), RumRemoveViewAttributePayload(name));
  }

  /** Creates a new 'StartResource' command. */
  static RumCommand StartResource(
      RumCommandParams&& base, std::string_view key, const RumRequestDetails& request
  ) {
    return RumCommand(std::move(base), RumStartResourcePayload(key, request));
  }

  /** Creates a new 'StopResource' command. */
  static RumCommand StopResource(
      RumCommandParams&& base,
      std::string_view key,
      const RumResponseDetails& response = RumResponseDetails(),
      const std::optional<RumErrorDetails>& error = std::nullopt
  ) {
    return RumCommand(std::move(base), RumStopResourcePayload(key, response, error));
  }

  /** Creates a new 'AddAction' command. */
  static RumCommand AddAction(
      RumCommandParams&& base, RumActionType type, std::string_view name
  ) {
    return RumCommand(std::move(base), RumAddActionPayload(type, name));
  }

  /** Creates a new 'StartAction' command. */
  static RumCommand StartAction(
      RumCommandParams&& base, RumActionType type, std::string_view name
  ) {
    return RumCommand(std::move(base), RumStartActionPayload(type, name));
  }

  /** Creates a new 'StopAction' command. */
  static RumCommand StopAction(RumCommandParams&& base, std::string_view name) {
    return RumCommand(std::move(base), RumStopActionPayload(name));
  }

  /** Creates a new 'AddError' command. */
  static RumCommand AddError(
      RumCommandParams&& base, RumErrorSource source, const RumErrorDetails& error
  ) {
    return RumCommand(std::move(base), RumAddErrorPayload(source, error));
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
