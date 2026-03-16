// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cinttypes>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "datadog/api.hpp"
#include "datadog/attribute.hpp"
#include "datadog/core.hpp"
#include "datadog/uuid.hpp"

namespace datadog {

// Forward declarations
namespace impl {
class Rum;
struct RumScopeDependencies;
}  // namespace impl

/**
 * Snapshot of essential RUM context state, capturing the current application,
 * session, view, and action identifiers.
 *
 * This structure is provided to context change callbacks to allow external
 * libraries to correlate their data with RUM state.
 */
struct RumContextSnapshot {
  /**
   * The RUM application ID. UUID::Zero if RUM is not initialized.
   */
  UUID application_id;

  /**
   * The current RUM session ID. UUID::Zero if no session is active.
   */
  UUID session_id;

  /**
   * The current RUM view ID. UUID::Zero if no view is active.
   */
  UUID view_id;

  /**
   * Name of the current RUM view. Points to an empty string if no view is
   * active (i.e., when `view_id` is UUID::Zero). When a view is active,
   * contains the explicit name provided to StartView(), or the key if no name
   * was provided.
   *
   * This pointer is valid only for the duration of the synchronous callback.
   * If the string value is needed beyond the callback's lifetime, it must be
   * copied.
   */
  const char* view_name;

  /**
   * The current RUM action ID. UUID::Zero if no action is active.
   */
  UUID action_id;

  bool operator==(const RumContextSnapshot& other) const {
    if (application_id != other.application_id || session_id != other.session_id ||
        view_id != other.view_id || action_id != other.action_id) {
      return false;
    }
    if (view_name == other.view_name) {
      return true;
    }
    if (!view_name || !other.view_name) {
      return false;
    }
    return std::strcmp(view_name, other.view_name) == 0;
  }

  bool operator!=(const RumContextSnapshot& other) const { return !(*this == other); }
};

/**
 * Callback function invoked when RUM context changes.
 *
 * The callback receives a snapshot of the new RUM context whenever any of the
 * context UUIDs (application_id, session_id, view_id, action_id) changes value,
 * including transitions to/from UUID::Zero.
 *
 * The callback is invoked synchronously on the thread that triggered the
 * context change. Callback implementations should be fast and non-blocking.
 */
using RumContextChangeCallback = std::function<void(const RumContextSnapshot&)>;


/**
 * Configures the details of the RUM feature upon initialization.
 */
struct RumConfig {
  friend class Rum;
  friend class impl::Rum;
  friend struct impl::RumScopeDependencies;

 private:
  UUID application_id;  // UUID::Zero if uninitialized or invalid
  float session_sample_rate{100.0f};
 public:
  /**
   * Initializes a new RUM configuration object with all required values.
   *
   * @param in_application_id The ID of your RUM Application. This value can be found
   *  under RUM Applications (https://app.datadoghq.com/rum/list), in the
   *  "SDK Configuration" settings for your Application.
   */
  DATADOG_API explicit RumConfig(std::string_view in_application_id);
  DATADOG_API explicit RumConfig(const UUID& in_application_id);

  // RumConfig is trivially destructible
  ~RumConfig() = default;

  // RumConfig is copyable and movable
  DATADOG_API RumConfig(const RumConfig&) noexcept;
  DATADOG_API RumConfig& operator=(const RumConfig&) noexcept;
  DATADOG_API RumConfig(RumConfig&&) noexcept;
  DATADOG_API RumConfig& operator=(RumConfig&&) noexcept;

  /**
   * Sets the RUM Application ID, overriding the value passed to the constructor.
   */
  DATADOG_API RumConfig& SetApplicationId(std::string_view value);
  DATADOG_API RumConfig& SetApplicationId(const UUID& value);

  /**
   * Sets the sample rate to a value between 0.0 and 100.0, indicating what percentage
   * of RUM sessions should be sampled. At 100.0, events for all sessions are sent to
   * intake; at 0.0, no RUM events are generated. Default is 100.0.
   */
  DATADOG_API RumConfig& SetSessionSampleRate(float value);
};

enum class RumActionType : uint8_t { Tap, Click, Scroll, Swipe, Custom };

/**
 * HTTP method used to initiate the retrieval of a resource.
 */
enum class RumResourceMethod : uint8_t {
  Get,
  Head,
  Post,
  Put,
  Delete,
  Connect,
  Options,
  Trace,
  Patch
};

/**
 * Type of resource retrieved; typically inferred from a response's Content-Type.
 */
enum class RumResourceType : uint8_t {
  Unknown,
  Beacon,
  Fetch,
  Xhr,
  Document,
  Native,
  Image,
  Js,
  Font,
  Css,
  Media,
  Other
};

/**
 * Describes the reason why a operation failed.
 *
 * This API is in preview and may change in future releases.
 */
enum class RumOperationFailureReason : uint8_t {
  /** Operation failed due to an error. */
  Error,
  /** Operation was abandoned (e.g. user navigated away). */
  Abandoned,
  /** Operation failed for another reason. */
  Other
};

/**
 * Describes a component of the application from which a RUM error originates.
 */
enum class RumErrorSource : uint8_t {
  Network,
  Source,
  Console,
  Logger,
  Agent,
  Webview,
  Custom,
  Report
};

/**
 * Interface to the Datadog SDK's RUM feature.
 */
class Rum {
 private:
  struct PrivateCtorTag {};

 public:
  // Callers should use Rum::Register
  explicit Rum(PrivateCtorTag);
  explicit Rum(
      std::shared_ptr<impl::Rum>&& impl,
      DiagnosticHandler diagnostic_handler,
      DiagnosticLevel diagnostic_threshold,
      PrivateCtorTag
  );
  DATADOG_API ~Rum();

 public:
  /**
   * Registers the RUM feature with the core of the Datadog SDK.
   */
  DATADOG_API static std::shared_ptr<Rum> Register(
      const std::shared_ptr<class Core>& core, const RumConfig& config
  );

  /**
   * Adds or updates a global attribute value that will be included with all RUM events
   * emitted hereafter.
   */
  DATADOG_API void AddAttribute(std::string_view name, const Attribute& value);

  /**
   * Removes a global attribute value, if one has been previously added with the given
   * name.
   */
  DATADOG_API void RemoveAttribute(std::string_view name);

  /**
   * Explicitly stops the current RUM session, if one is active.
   *
   * Once a session has been explicitly stopped, the next call to StartView(),
   * StartAction(), or AddAction() will automatically start a new session. If that new
   * session is created in response to an action, the last active view from the previous
   * session will be restarted in the new session.
   */
  DATADOG_API void StopSession();

  /**
   * Starts a new RUM view, recording that the user has navigated to the portion of the
   * application uniquely identified by the given string key.
   *
   * If no RUM session is currently active, starting a view will implicitly create a new
   * session.
   *
   * When a new view is started, all existing views are implicitly stopped.
   *
   * @param name An optional human-readable view name to be used in the Datadog UI. If
   *  not specified, defaults to the value of `key`.
   * @param attributes An optional set of custom attributes to associate with the view.
   */
  DATADOG_API void StartView(
      std::string_view key,
      std::string_view name = std::string_view{},
      const Attribute& attributes = Attribute()
  );

  /**
   * Adds or updates a custom attribute value stored in the context of the current view.
   *
   * All events produced within the context of a view will include the set of custom
   * attributes formed from both global and view-level attributes, with view attributes
   * taking precedence in the case of name conflicts.
   *
   * View attributes are scoped to the lifetime of the view and do not persist to
   * subsequent views.
   */
  DATADOG_API void AddViewAttribute(std::string_view name, const Attribute& value);

  /**
   * Removes any custom attribute value stored under the given name in the context of
   * the current view.
   *
   * If the view-level attribute being removed was shadowing a global attribute of the
   * same name, subsequent events will once again use the global attribute value.
   */
  DATADOG_API void RemoveViewAttribute(std::string_view name);

  /**
   * Stops any active RUM views that are identified with the given key.
   */
  DATADOG_API void StopView(
      std::string_view key, const Attribute& attributes = Attribute()
  );

  /**
   * Records a discrete user action of the given type in the context of the current
   * view. A name is required, and the provided name will be used to identify the target
   * of the action in the Datadog UI.
   *
   * Discrete actions (i.e. those added via AddAction()) are momentary: they do not
   * require an explicit call to StopAction().
   *
   * Discrete actions with type RumActionType::Custom are reported immediately, and they
   * may be recorded via AddAction() at any time, regardless of whether another action
   * is curently active.
   *
   * Discrete actions of all other types will remain active for at least 100ms prior to
   * being reported, so that any RUM resources or errors occurring immediately after the
   * action may be correlated with the action.
   *
   * If StartResource() calls occur while the action is active, the action may remain
   * active until the corresponding StopResource() calls are made, even if those
   * resources remain active after the initial 100ms timeout. However, an explicit
   * StopAction() call will always stop the action, regardless of whether it's waiting
   * for resources to complete.
   *
   * Only one action may be active at any given time: if you call AddAction() or
   * StartAction() while another action is active, the new action will be ignored (with
   * the exception of AddAction() with RumActionType::Custom as described above).
   */
  DATADOG_API void AddAction(
      RumActionType type,
      std::string_view name,
      const Attribute& attributes = Attribute()
  );

  /**
   * Records a continuous user action of the given type in the context of the current
   * view. A name is required, and the provided name will be used to identify the target
   * of the action in the Datadog UI.
   *
   * A continuous user action will remain active until StopAction() is called, or until
   * a timeout duration (of at least 10 seconds) has elapsed without a call to
   * StopAction().
   *
   * If StartResource() calls occur while the action is active, the action may remain
   * active until the corresponding StopResource() calls are made, even if those
   * resources remain active after the initial 10-second timeout. However, an explicit
   * StopAction() call will always stop the action, regardless of whether it's waiting
   * for resources to complete.
   *
   * Only one action may be active at a time. If you call StartAction() while another
   * action is already active, regardless of the type you pass to StartAction(), the new
   * action will be ignored.
   */
  DATADOG_API void StartAction(
      RumActionType type,
      std::string_view name,
      const Attribute& attributes = Attribute()
  );

  /**
   * Stops the currently-active action, if any exists in the current view.
   *
   * By convention, the provided `type` value is expected to match the value passed to
   * StartAction() for the action that you intend to stop. However, this value is
   * currently ignored.
   *
   * A name value is not required. If provided, the active action will have its name
   * changed to the provided value before it is stopped. If empty, the active action
   * will be stopped without any change to its name.
   *
   * StopAction() will always stop the active action, regardless of whether it was
   * created via AddAction() or StartAction(), regardless of its type and name, and
   * regardless of whether the action is waiting for associated RUM resources to be
   * stopped.
   */
  DATADOG_API void StopAction(
      RumActionType type,
      std::string_view name = {},
      const Attribute& attributes = Attribute()
  );

  /**
   * Records that the application has initiated an HTTP request, causing that request to
   * be tracked as a RUM Resource in the context of the current view.
   *
   * @param key - An arbitrary string that uniquely identifies this specific HTTP
   *  request. Must be unique among all concurrent requests. Must be non-empty.
   * @param method - The HTTP request method used.
   * @param url - The request URL. Conventionally, this is an absolute URL. Must be
   *  non-empty.
   * @param attributes - An optional set of custom attributes describing the resource,
   *  provided as an Attribute with ValueType::Object.
   */
  DATADOG_API void StartResource(
      std::string_view key,
      RumResourceMethod method,
      std::string_view url,
      const Attribute& attributes = Attribute()
  );

  /**
   * Records that an HTTP request has been completed and has received a valid response.
   * A response with a 400-level or 500-level status code is still considered a valid
   * response, provided that the application encountered no errors while processing it.
   *
   * @param key - The unique identifier that was used when the target resource was
   *  started. Must be non-empty.
   * @param status_code - The HTTP status code received from the response. If unknown,
   *  supply 0.
   * @param size - The total size of the resource. Conventionally, this is the decoded
   *  size of the response body in bytes; i.e. the response size after decompression,
   *  excluding headers and framing overhead.
   * @param type - The kind of resource that was retrieved by this request.
   * @param attributes - An optional set of custom attributes describing the resource,
   *  provided as an Attribute with ValueType::Object, to be merged with any custom
   *  attribute values provided when the resource was started.
   */
  DATADOG_API void StopResource(
      std::string_view key,
      int32_t status_code = 0,
      int64_t size = -1,
      RumResourceType type = RumResourceType::Unknown,
      const Attribute& attributes = Attribute()
  );

  /**
   * Records that an HTTP request could not be completed due to an error (and therefore
   * no response was received), or that processing of the response failed due to an
   * error.
   *
   * @param key - The unique identifier that was used when the target resource was
   *  started. Must be non-empty.
   * @param error_message - A string describing the error. Should be non-empty.
   * @param error_type - A name describing the error's type. May be empty.
   * @param error_stack_trace - The full text of a stack trace describing the context
   * for the error. May be empty.
   * @param is_network_error - Whether the request failed due to a network connection
   *  issue, such as DNS lookup failure, connection timeout, etc. Used to categorize the
   *  resulting RUM Error in either the "network" or "exception" category.
   * @param status_code - The HTTP status code received from the response, if a response
   *  was received.
   * @param attributes - An optional set of custom attributes describing the resource,
   *  provided as an Attribute with ValueType::Object, to be merged with any custom
   *  attribute values provided when the resource was started.
   */
  DATADOG_API void StopResourceWithError(
      std::string_view key,
      std::string_view error_message,
      std::string_view error_type = {},
      std::string_view error_stack_trace = {},
      bool is_network_error = false,
      int32_t status_code = 0,
      const Attribute& attributes = Attribute()
  );

  /**
   * Records that the application has encountered an error in the context of the current
   * view.
   *
   * @param source - The source of the error. If in doubt: 'source' covers errors due to
   *  bugs in the application's source; 'network' covers network issues; and 'custom' is
   *  resonable catch-all.
   * @param message - A string describing the error. Should be non-empty.
   * @param type - A name describing the error's type. May be empty.
   * @param stack_trace - The full text of a stack trace describing the context for the
   *  error. May be empty.
   * @param attributes - An optional set of custom attributes describing the error,
   *  provided as an Attribute with ValueType::Object.
   */
  DATADOG_API void AddError(
      RumErrorSource source,
      std::string_view message,
      std::string_view type = {},
      std::string_view stack_trace = {},
      const Attribute& attributes = Attribute()
  );

  /**
   * Records the start of a operation (e.g. login, checkout, upload).
   *
   * Each call emits a vital operation step event with step_type "start". The backend
   * aggregates start/stop steps into full operations with computed duration and success
   * rate.
   *
   * This API is in preview and may change in future releases.
   *
   * @param name - The name of the operation. Must be non-empty.
   * @param operation_key - An optional key to distinguish parallel instances of the
   * same operation. Must be non-empty if provided.
   * @param attributes - An optional set of custom attributes for this event.
   */
  DATADOG_API void StartFeatureOperation(
      std::string_view name,
      std::string_view operation_key = {},
      const Attribute& attributes = Attribute()
  );

  /**
   * Records the successful completion of a operation.
   *
   * Each call emits a vital operation step event with step_type "end" and no failure
   * reason.
   *
   * This API is in preview and may change in future releases.
   *
   * @param name - The name of the operation. Must be non-empty.
   * @param operation_key - An optional key to distinguish parallel instances of the
   * same operation. Must be non-empty if provided.
   * @param attributes - An optional set of custom attributes for this event.
   */
  DATADOG_API void SucceedFeatureOperation(
      std::string_view name,
      std::string_view operation_key = {},
      const Attribute& attributes = Attribute()
  );

  /**
   * Records the failure of a operation.
   *
   * Each call emits a vital operation step event with step_type "end" and the specified
   * failure reason.
   *
   * This API is in preview and may change in future releases.
   *
   * @param name - The name of the operation. Must be non-empty.
   * @param failure_reason - The reason the operation failed.
   * @param operation_key - An optional key to distinguish parallel instances of the
   * same operation. Must be non-empty if provided.
   * @param attributes - An optional set of custom attributes for this event.
   */
  DATADOG_API void FailFeatureOperation(
      std::string_view name,
      RumOperationFailureReason failure_reason,
      std::string_view operation_key = {},
      const Attribute& attributes = Attribute()
  );

 private:
  // Forbid copying/moving: we use std::shared_ptr<Rum> at the API boundary
  Rum(const Rum&) = delete;
  Rum& operator=(const Rum&) = delete;
  Rum(Rum&&) = delete;
  Rum& operator=(Rum&&) = delete;

  std::shared_ptr<impl::Rum> _impl;
  DiagnosticHandler _diagnostic_handler;
  DiagnosticLevel _diagnostic_threshold;
};

}  // namespace datadog
