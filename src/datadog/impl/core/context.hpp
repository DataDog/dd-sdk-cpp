// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>

#include "datadog/attribute.hpp"
#include "datadog/core.hpp"
#include "datadog/timestamp.hpp"

#include "datadog/impl/core/feature_types/rum.hpp"

namespace datadog::platform {
struct OsInfo;
struct DeviceInfo;
}  // namespace datadog::platform

namespace datadog::impl {

class MessageBus;

/**
 * Details of the user who's interacting with the application, provided via the UserInfo
 * APIs.
 *
 * A call to SetUserInfo() overwrites all values held in UserInfo. Any value may be
 * empty, indicating that the application supplied no value for that property.
 * Subsequent calls to AddUserExtraInfo() merge additional attribute values into
 * `extra`.
 *
 * Check IsEmpty() to determine whether any values are set. If UserInfo is entirely
 * empty, no "usr" value should be written in events.
 */
struct UserInfo {
  std::string id;     // ID passed to SetUserInfo, or empty
  std::string name;   // Name passed to last SetUserInfo call, or empty
  std::string email;  // Email passed to last SetUserInfo call, or empty
  Attribute extra;    // Object containing arbitrary extra properties

  bool IsEmpty() const {
    return id.empty() && name.empty() && email.empty() &&
           extra.GetObjectPropertyCount() == 0;
  }
};

/**
 * Details of the account being used to interact with the application.
 */
struct AccountInfo {
  std::string id;
  std::string name;
  Attribute extra;

  bool IsEmpty() const {
    return id.empty() && name.empty() && extra.GetObjectPropertyCount() == 0;
  }
};

/**
 * Subset of CoreContext values that never change after SDK initialization.
 *
 * The Core holds a value of this type, and each CoreContext snapshot contains
 * lightweight views (std::string_view, stable raw ptr, etc.) of these values.
 */
struct ImmutableContext {
  const platform::OsInfo* os;          // Member of Core-owned ISystemInfo; always valid
  const platform::DeviceInfo* device;  // Member of Core-owned ISystemInfo; always valid

  /**
   * Wall-clock time at which the current process was launched, as nanoseconds since
   * the Unix epoch. Zero if the launch time could not be determined.
   */
  Timestamp process_launch_time;

  std::string_view client_token;         // Required member of Core-owned CoreConfig
  std::string_view service;              // Required member of Core-owned CoreConfig
  std::string_view env;                  // Required member of Core-owned CoreConfig
  std::string_view application_version;  // Optional member of Core-owned CoreConfig
  std::string_view variant;              // Optional member of Core-owned CoreConfig

  std::string source;         // Internal cross-platform-SDK override, or "cpp"
  std::string sdk_version;    // Internal cross-platform-SDK override, or SDK_VERSION
  std::string intake_origin;  // Derived from configured site or custom endpoint URL
  std::string user_agent;     // Built from config + HTTP client + DeviceInfo + OsInfo
  std::string per_event_ddtags;  // 'service:<service>,env:<env>' etc.

  /**
   * Initializes the full set of ImmutableContext values given a set of values and
   * dependencies used to initialize the Core.
   *
   * All input values must outlive `ImmutableContext`.
   */
  explicit ImmutableContext(
      const CoreConfig& config,
      const platform::OsInfo& os_info,
      const platform::DeviceInfo& device_info,
      Timestamp process_launch_time,
      std::string_view http_subsystem_name,
      std::string_view http_subsystem_version
  );
};

/**
 * Global context shared across the entire SDK instance.
 */
struct CoreContext {
  /**
   * Initializes a new CoreContext from the provided SDK config, OS info, and device
   * info.
   */
  explicit CoreContext(
      const ImmutableContext& im, TrackingConsent initial_tracking_consent
  );

  // === Lightweight views of ImmutableContext members owned by the Core ===

  /**
   * Operating system information collected at SDK startup. Borrowed from ISystemInfo
   * subsystem, which is tied to the lifetime of the Core. Guaranteed to be valid and
   * non-null while the Core is in operation.
   */
  const platform::OsInfo* os;

  /**
   * Device information collected at SDK startup. Borrowed from ISystemInfo subsystem,
   * which is tied to the lifetime of the Core. Guaranteed to be valid and non-null
   * while the Core is in operation.
   */
  const platform::DeviceInfo* device;

  /**
   * Wall-clock time at which the current process was launched, as nanoseconds since
   * the Unix epoch — on the same time basis as IClock::Now(). Zero if the launch time
   * could not be determined.
   */
  Timestamp process_launch_time;

  /**
   * Client token configured for this SDK instance, used to authorize HTTP requests.
   * Never empty for a valid Core.
   */
  std::string_view client_token;

  /**
   * Service name associated with the instrumented application, for unified tagging.
   * Never empty for a valid Core.
   */
  std::string_view service;

  /**
   * Environment in which the application is running, for unified tagging. Never empty
   * for a valid Core.
   */
  std::string_view env;

  /**
   * Version associated with this build of the instrumented application, for unified
   * tagging. May be empty.
   */
  std::string_view application_version;

  /**
   * Build variant, config, or flavor describing the current application binary, for
   * unified tagging. May be empty.
   */
  std::string_view variant;

  /**
   * Internal value identifying the Datadog SDK responsible for instrumenting this
   * application: defaults to 'cpp' for natively-instrumented apps, but may be
   * overridden by cross-platform SDKs.
   */
  std::string_view source;

  /**
   * String indicating the version of the Datadog SDK in use: equivalent to SDK_VERSION
   * if source is 'cpp', but may be overridden along with source.
   */
  std::string_view sdk_version;

  /**
   * HTTP origin describing the Datadog intake endpoint where this SDK instance sends
   * event batches at upload, derived from configured site or custom endpoint.
   */
  std::string_view intake_origin;

  /**
   * Value used for User-Agent header, encoding basic information about the application,
   * the HTTP client implementation, and the system on which the application is running,
   * e.g. `my.service/1.0.0 libcurl/8.11.0 (ThinkPad-T14-Gen-2; Ubuntu/22.04);`
   */
  std::string_view user_agent;

  /**
   * Unified service tags applied to all event payloads in a 'ddtags' property, e.g.
   * 'service:foo,env:bar,...'; not to be confused with the upload-level retry telemetry
   * values that use the same name and format (e.g. '?ddtags=retry_count:N,...').
   */
  std::string_view per_event_ddtags;

  // === Essential SDK state that may change during SDK operation ===

  /**
   * Current tracking consent state for this SDK instance. Initialized from `CoreConfig`
   * and updated whenever the application invokes `Core::SetTrackingConsent()`.
   */
  TrackingConsent tracking_consent;

  /**
   * User info set via Core::SetUserInfo(), if any. If non-empty, used to populate `usr`
   * field on outgoing RUM and Log events.
   */
  UserInfo user_info;

  /**
   * Account info set via Core::SetAccountInfo(), if any.
   */
  AccountInfo account_info;

  /**
   * Anonymous user id resolved once by Core::Init() from a file persisted in the
   * '.core' artifact storage directory. UUID::Zero if resolution failed (e.g.
   * filesystem error) or hasn't happened yet. Effectively immutable after Init()
   * completes, but lives here (rather than ImmutableContext) because
   * SdkStorage/ArtifactStorage, which resolution depends on, aren't available until
   * Init() runs, after CoreContext already exists.
   */
  UUID anonymous_id;

  /**
   * Whether `anonymous_id` should be exposed as `usr.anonymous_id` on RUM and Log
   * events. False until RUM registers and starts with `trackAnonymousUser` set to
   * true; unaffected by CoreContext::Reset(), so it stays true across Stop()/Start()
   * cycles once set.
   */
  bool anonymous_id_enabled{false};

  // === Feature-specific context values that may change during SDK operation ===

  /**
   * Additional context provided by the RUM feature, if in use.
   */
  std::optional<RumFeatureContext> rum;

  /**
   * Resets all mutable feature-specific context fields to their default (absent) state.
   * Called by Core at the start of each run to ensure features never inherit stale
   * context from a previous run.
   */
  void Reset();
};

/**
 * Owns a CoreContext and provides thread-safe access to that value.
 */
class CoreContextProvider {
 private:
  CoreContext _context;
  mutable std::shared_mutex _mutex;
  MessageBus* _message_bus{nullptr};

 public:
  explicit CoreContextProvider(const CoreContext& context);

  /**
   * Returns an immutable, thread-safe copy of the current CoreContext value.
   */
  CoreContext Get() const;

  /**
   * Mutates the current CoreContext value, by invoking the provided callback after
   * obtaining exclusive write access.
   *
   * After mutation, dispatches a `ContextChangedMessage` on the `MessageBus`, carrying
   * an immutable snapshot of the updated context to any Features that have registered
   * an interest in receiving notifications of context changes.
   */
  void Update(const std::function<void(CoreContext&)>& callback);

  /**
   * Registers the `MessageBus` that `Update()` will dispatch `ContextChangedMessage`
   * values to. Pass `nullptr` to detach the bus (e.g. during shutdown). Called by
   * `Core::Start()` before the context thread launches; no synchronization is needed
   * because the context thread is not yet running at that point.
   */
  void SetMessageBus(MessageBus* bus);
};

}  // namespace datadog::impl
