// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <array>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

// Win32 preprocessor defines must be set before Crashpad includes
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#endif

#include "client/annotation.h"
#include "client/crash_report_database.h"
#include "client/crashpad_client.h"
#include "client/settings.h"

#include "datadog/impl/core/events/omissible.hpp"
#include "datadog/impl/core/events/struct.hpp"
#include "datadog/impl/core/feature_types/rum.hpp"
#include "datadog/impl/core/json/attribute.hpp"
#include "datadog/impl/core/storage/path.hpp"
#include "datadog/impl/core/util/diagnostics.hpp"
#include "datadog/impl/core/util/json.hpp"
#include "datadog/impl/crash_reporting/crash_handler.hpp"
#include "datadog/impl/crash_reporting/handlers/crashpad/view_event_fit.hpp"

#ifdef _WIN32
#include <windows.h>  // GetModuleFileName
#else
#include <limits.h>  // PATH_MAX
#ifdef __APPLE__
#include <mach-o/dyld.h>  // _NSGetExecutablePath
#endif
#endif

// Crashpad Annotations: these values are stored in static, fixed-size buffers, making
// them safe to read during a crash. The Crashpad handler will automatically resolve
// these values and include them as annotations when the crash dump is uploaded.
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
static crashpad::StringAnnotation<16> s_dd_tracking_consent("dd.tracking_consent");
static crashpad::StringAnnotation<512> s_dd_config("dd.config");
static crashpad::StringAnnotation<256> s_dd_os("dd.os");
static crashpad::StringAnnotation<512> s_dd_device("dd.device");
static crashpad::StringAnnotation<2048> s_dd_usr("dd.usr");
static crashpad::StringAnnotation<2048> s_dd_account("dd.account");
static crashpad::StringAnnotation<128> s_dd_rum_config("dd.rum.config");
static crashpad::StringAnnotation<192> s_dd_rum_session("dd.rum.session");
static crashpad::StringAnnotation<4096> s_dd_rum_attributes("dd.rum.attributes");
static crashpad::StringAnnotation<8192> s_dd_rum_last_view("dd.rum.last_view");
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

/**
 * Returns the path to the executable that is currently running this code, or an empty
 * path if unable to resolve the executable path.
 */
static std::filesystem::path get_current_executable_path() {
  // NOLINTBEGIN(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
#ifdef _WIN32
  char result[MAX_PATH];
  int length = GetModuleFileName(NULL, result, MAX_PATH);
  if (length == 0 || length == MAX_PATH) {
    return "";
  }
  return std::filesystem::path(result);
#elif __APPLE__
  char buf[PATH_MAX];
  uint32_t bufsize = PATH_MAX;
  if (!_NSGetExecutablePath(buf, &bufsize)) {
    return std::filesystem::path(buf);
  }
  return "";
#else
  char result[PATH_MAX];
  ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
  if (count == -1) {
    return "";
  }
  return std::filesystem::path(std::string(result, count));
#endif
  // NOLINTEND(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
}

/**
 * Returns the path to the crashpad_handler executable. By convention, the handler
 * executable is deployed to the same directory as the application binary.
 */
static std::filesystem::path get_crashpad_handler_path() {
  auto current_exe_path = get_current_executable_path();
#ifdef _WIN32
  return current_exe_path.parent_path() / "crashpad_handler.exe";
#else
  return current_exe_path.parent_path() / "crashpad_handler";
#endif
}

namespace datadog::impl {

// Lightweight structs used to serialize CrashContext fields into Crashpad string
// annotations, as JSON values

/**
 * A value encoded in `dd.rum.config`.
 */
struct RumConfigAnnotation {
  OmitIfZero<UUID> application_id;
  float session_sample_rate;
};
DATADOG_JSON_STRUCT(
    RumConfigAnnotation,
    DATADOG_JSON_FIELD(application_id),
    DATADOG_JSON_FIELD(session_sample_rate)
)

/**
 * A value encoded in `dd.rum.session`.
 */
struct RumSessionAnnotation {
  OmitIfZero<UUID> id;
  bool is_sampled;
  bool is_active;
  bool is_initial;
  bool has_tracked_any_view;
  bool did_start_with_replay;
};
DATADOG_JSON_STRUCT(
    RumSessionAnnotation,
    DATADOG_JSON_FIELD(id),
    DATADOG_JSON_FIELD(is_sampled),
    DATADOG_JSON_FIELD(is_active),
    DATADOG_JSON_FIELD(is_initial),
    DATADOG_JSON_FIELD(has_tracked_any_view),
    DATADOG_JSON_FIELD(did_start_with_replay)
)

/**
 * A value encoded in `dd.usr`.
 */
struct UsrAnnotation {
  OmitIfEmpty<std::string_view> id;
  OmitIfEmpty<std::string_view> name;
  OmitIfEmpty<std::string_view> email;
  OmitIfZero<UUID> anonymous_id;
  const Attribute& extra;
};
DATADOG_JSON_STRUCT_WITH_EXTRA_ATTRIBUTES(
    UsrAnnotation,
    extra,
    DATADOG_JSON_FIELD(id),
    DATADOG_JSON_FIELD(name),
    DATADOG_JSON_FIELD(email),
    DATADOG_JSON_FIELD(anonymous_id)
)

/**
 * A value encoded in `dd.account`.
 */
struct AccountAnnotation {
  OmitIfEmpty<std::string_view> id;
  OmitIfEmpty<std::string_view> name;
  const Attribute& extra;
};
DATADOG_JSON_STRUCT_WITH_EXTRA_ATTRIBUTES(
    AccountAnnotation, extra, DATADOG_JSON_FIELD(id), DATADOG_JSON_FIELD(name)
)

/**
 * A value encoded in `dd.config`.
 */
struct ConfigAnnotation {
  std::string_view service;
  std::string_view env;
  std::string_view version;
  std::string_view variant;
  std::string_view source;
  std::string_view sdk_version;
};
DATADOG_JSON_STRUCT(
    ConfigAnnotation,
    DATADOG_JSON_FIELD(service),
    DATADOG_JSON_FIELD(env),
    DATADOG_JSON_FIELD(version),
    DATADOG_JSON_FIELD(variant),
    DATADOG_JSON_FIELD(source),
    DATADOG_JSON_FIELD(sdk_version)
)

/**
 * A value encoded in `dd.os`.
 */
struct OsAnnotation {
  std::string_view name;
  std::string_view version;
  std::string_view build;
  std::string_view version_major;
};
DATADOG_JSON_STRUCT(
    OsAnnotation,
    DATADOG_JSON_FIELD(name),
    DATADOG_JSON_FIELD(version),
    DATADOG_JSON_FIELD(build),
    DATADOG_JSON_FIELD(version_major)
)

/**
 * A value encoded in `dd.device`.
 */
struct DeviceAnnotation {
  std::string_view type;
  std::string_view name;
  std::string_view model;
  std::string_view brand;
  std::string_view architecture;
  std::string_view locale;
  std::string_view time_zone;
};
DATADOG_JSON_STRUCT(
    DeviceAnnotation,
    DATADOG_JSON_FIELD(type),
    DATADOG_JSON_FIELD(name),
    DATADOG_JSON_FIELD(model),
    DATADOG_JSON_FIELD(brand),
    DATADOG_JSON_FIELD(architecture),
    DATADOG_JSON_FIELD(locale),
    DATADOG_JSON_FIELD(time_zone)
)

/**
 * Serializes `value` as JSON into a stack-allocated buffer and sets the given Crashpad
 * string annotation to the result. If encoding fails because the value is too large to
 * fit in the buffer, resets the annotation to `{}` (clearing any previously-set value)
 * and logs an error once (guarded by `out_logged_error`).
 */
template <uint32_t N, typename T>
void TrySetAnnotation(
    crashpad::StringAnnotation<N>& annotation,
    const T& value,
    const DiagnosticLogger& logger,
    bool& out_logged_error
) {
  std::array<char, N> buf{};
  if (auto written = TryEncodeJson(buf.data(), N, value)) {
    annotation.Set(std::string_view(buf.data(), *written));
  } else {
    annotation.Set("{}");
    if (!out_logged_error) {
      out_logged_error = true;
      logger.Error(
          "Failed to encode Crashpad annotation: value too large for buffer",
          {{"annotation", annotation.name()}}
      );
    }
  }
}

/**
 * Serializes `value` (a struct with extra attributes) as JSON into a stack-allocated
 * buffer and sets the given Crashpad string annotation to the result. If encoding fails
 * because the base struct is too large to fit, resets the annotation to `{}` (clearing
 * any previously-set value) and logs an error once (guarded by `out_logged_error`). If
 * extra attributes were dropped to fit the buffer, logs a warning once (guarded by
 * `out_logged_truncation_warning`).
 */
template <uint32_t N, typename T>
void TrySetAnnotationWithExtras(
    crashpad::StringAnnotation<N>& annotation,
    const T& value,
    const DiagnosticLogger& logger,
    bool& out_logged_error,
    bool& out_logged_truncation_warning
) {
  std::array<char, N> buf{};
  const auto result = TryEncodeJson(buf.data(), N, value);
  if (!result) {
    annotation.Set("{}");
    if (!out_logged_error) {
      out_logged_error = true;
      logger.Error(
          "Failed to encode Crashpad annotation: value too large for buffer",
          {{"annotation", annotation.name()}}
      );
    }
  } else {
    annotation.Set(std::string_view(buf.data(), result->bytes_written));
    if (result->truncated && !out_logged_truncation_warning) {
      out_logged_truncation_warning = true;
      logger.Warning(
          "Crashpad annotation truncated: extra attributes dropped to fit buffer",
          {{"annotation", annotation.name()}}
      );
    }
  }
}

/**
 * Fits the given RUM View event JSON into the given Crashpad string annotation,
 * dropping context attributes as needed. If the input is empty, sets the annotation
 * to `{}`. If the event is too large to fit even after truncation, resets the
 * annotation to `{}` and logs an error once. If context was dropped to fit, logs a
 * truncation warning once.
 */
template <uint32_t N>
void TrySetLastViewAnnotation(
    crashpad::StringAnnotation<N>& annotation,
    std::string_view last_view_event_json,
    const DiagnosticLogger& logger,
    bool& out_logged_error,
    bool& out_logged_truncation_warning
) {
  if (last_view_event_json.empty()) {
    annotation.Set("{}");
    return;
  }
  std::array<char, N> buf{};
  const auto result = FitViewEventToBuffer(last_view_event_json, buf);
  annotation.Set(result.value);
  if (result.status == FitViewEventResult::Status::Dropped) {
    if (!out_logged_error) {
      out_logged_error = true;
      logger.Error(
          "Failed to encode Crashpad annotation: last view event too large for "
          "buffer",
          {{"annotation", annotation.name()}}
      );
    }
  } else if (result.status == FitViewEventResult::Status::Truncated) {
    if (!out_logged_truncation_warning) {
      out_logged_truncation_warning = true;
      logger.Warning(
          "Crashpad annotation truncated: last view event context dropped to fit "
          "buffer",
          {{"annotation", annotation.name()}}
      );
    }
  }
}

/**
 * Crash handler implementation that uses the Crashpad client library, in conjunction
 * with an external, out-of-process crashpad_handler executable that will be spawned by
 * the client library on Initialize().
 *
 * When a crash occurs, the Crashpad client's signal handlers detect the crash, collect
 * register state, and send an IPC notification to the handler process. The handler
 * process then captures a Breakpad-format minidump from the crashing application
 * process and uploads it to the configured intake URL.
 *
 * NOTE: COMPILING WITH DD_CRASH_MODE=crashpad IS NOT YET SUPPORTED. This is a prototype
 * implementation that does not yet report crashes to Datadog intake.
 */
class CrashpadCrashHandler final : public ICrashHandler {
 public:
  CrashpadCrashHandler() = default;

  bool Initialize(
      DiagnosticLogger logger,
      IFilesystem& fs,
      const StoragePath& crash_storage_dir_path,
      std::string_view helper_exe_path,
      std::string_view upload_origin
  ) override {
    (void)fs;

    // Prepare Crashpad client options
    std::filesystem::path crashpad_handler_path = helper_exe_path;
    if (crashpad_handler_path.empty()) {
      crashpad_handler_path = get_crashpad_handler_path();
    }
    std::filesystem::path crashpad_database_path = crash_storage_dir_path.Get();
    const std::string url =
        std::string(upload_origin) + "/crashpad-ingest-placeholder-path";
    std::map<std::string, std::string> annotations;
    std::vector<std::string> arguments;
    const bool restartable = false;
    const bool asynchronous_start = false;
    std::vector<base::FilePath> attachments;

    // Attempt to start the Crashpad handler process and initialize the client library
    crashpad::CrashpadClient crashpad_client;
    const bool started = crashpad_client.StartHandler(
        base::FilePath(crashpad_handler_path),
        base::FilePath(crashpad_database_path),
        base::FilePath(crashpad_database_path),
        url,
        annotations,
        arguments,
        restartable,
        asynchronous_start,
        attachments
    );
    if (!started) {
      logger.Error("Failed to start Crashpad handler");
      return false;
    }

    // Store the DiagnosticLogger so we can use it to log diagnostic messages if we
    // receive data in SetCrashContext that can't be fully serialized to Crashpad
    // annotations
    _logger = logger;

    // Initialize annotation values to their defaults
    s_dd_tracking_consent.Set("pending");
    s_dd_config.Set("{}");
    s_dd_os.Set("{}");
    s_dd_device.Set("{}");
    s_dd_usr.Set("{}");
    s_dd_account.Set("{}");
    s_dd_rum_config.Set("{}");
    s_dd_rum_session.Set("{}");
    s_dd_rum_attributes.Set("{}");
    s_dd_rum_last_view.Set("{}");

    // When the Crashpad client is first initialized, it populates the configured
    // database directory with configuration metadata and other state. By default, a
    // database is configured to rate-limit uploads to once per hour, and uploads are
    // also entirely disabled by default.
    auto db = crashpad::CrashReportDatabase::Initialize(
        base::FilePath(crashpad_database_path)
    );
    if (db) {
      // Explicitly enable uploads so that new crashes will be POSTed to our upload URL
      // if not rate-limited
      if (auto* settings = db->GetSettings(); settings) {
        settings->SetUploadsEnabled(true);
      }
    }

    // Example upload behavior: if Crashpad produces a minidump with GUID
    // 617ab41b-84d0-472f-b261-bae41acb901a, and it's configured with an upload URL of
    // https://example.com/dumps/upload, along with the dd.tracking_consent annotation
    // (see s_dd_tracking_consent above), then the handler will initiate an HTTP POST
    // request equivalent to:
    //
    // clang-format off
    // ================================================================================
    // POST /dumps/upload?guid=617ab41b-84d0-472f-b261-bae41acb901a HTTP/1.1
    // Host: example.com
    // Content-Type: multipart/form-data; boundary=---MultipartBoundary-8kqZEVmthMNNvtlBr6K4bPibxe2jX64I---
    // Transfer-Encoding: chunked
    // Accept: */*
    // Content-Encoding: gzip
    // User-Agent: Crashpad/0.8.0 CFNetwork/3826.600.41 Darwin/24.6.0 (arm64)
    // Accept-Language: en-US,en;q=0.9
    // Accept-Encoding: gzip, deflate
    // Connection: keep-alive
    //
    // ---MultipartBoundary-8kqZEVmthMNNvtlBr6K4bPibxe2jX64I---
    // Content-Disposition: form-data; name="guid"
    //
    // 617ab41b-84d0-472f-b261-bae41acb901a
    // ---MultipartBoundary-8kqZEVmthMNNvtlBr6K4bPibxe2jX64I---
    // Content-Disposition: form-data; name="dd.tracking_consent"
    //
    // granted
    // ---MultipartBoundary-8kqZEVmthMNNvtlBr6K4bPibxe2jX64I---
    // Content-Disposition: form-data; name="upload_file_minidump"; filename="6af03cf2-c984-4257-a2a3-304033a95b0b.dmp"
    // Content-Type: application/octet-stream
    //
    // [... raw bytes of minidump file ...]
    // ---MultipartBoundary-8kqZEVmthMNNvtlBr6K4bPibxe2jX64I---
    // ================================================================================
    // clang-format on
    return true;
  }

  void SetCrashContext(IFilesystem& fs, const CrashContext& ctx) override {
    // We don't need to persist context to disk: we just set crashpad annotation values,
    // which the crashpad_handler executable will capture from process memory on crash
    (void)fs;

    // Set dd.tracking_consent, so the handler's processing/upload of the crash can be
    // conditioned upon user tracking consent
    const TrackingConsent consent = ctx.tracking_consent;
    if (consent != _tracking_consent) {
      _tracking_consent = consent;
      switch (consent) {
        case TrackingConsent::Pending:
          s_dd_tracking_consent.Set("pending");
          break;
        case TrackingConsent::Granted:
          s_dd_tracking_consent.Set("granted");
          break;
        case TrackingConsent::NotGranted:
          s_dd_tracking_consent.Set("not-granted");
          break;
      }
    }

    // Set dd.config, so the handler has the essential details of SDK config needed to
    // generate new RUM events and/or construct upload URLs with appropriate params
    TrySetAnnotation(
        s_dd_config,
        ConfigAnnotation{
            ctx.service,
            ctx.env,
            ctx.application_version,
            ctx.variant,
            ctx.source,
            ctx.sdk_version,
        },
        _logger,
        _logged_config_error
    );

    // Set dd.os, so the handler can populate the 'os' property on RUM events if it
    // needs to create them from scratch
    TrySetAnnotation(
        s_dd_os,
        OsAnnotation{
            ctx.os_name,
            ctx.os_version,
            ctx.os_build,
            ctx.os_version_major,
        },
        _logger,
        _logged_os_error
    );

    // Set dd.device, so the handler can populate RUM events' 'device' property
    TrySetAnnotation(
        s_dd_device,
        DeviceAnnotation{
            ctx.device_type,
            ctx.device_name,
            ctx.device_model,
            ctx.device_brand,
            ctx.device_architecture,
            ctx.device_locale,
            ctx.device_time_zone,
        },
        _logger,
        _logged_device_error
    );

    // Set dd.usr, so the handler can populate 'usr': if the application has provided
    // more extra user attributes than will fit in our Crashpad annotation buffer, extra
    // annotations will be dropped from the back
    TrySetAnnotationWithExtras(
        s_dd_usr,
        UsrAnnotation{
            ctx.user_id,
            ctx.user_name,
            ctx.user_email,
            ctx.user_anonymous_id,
            ctx.user_extra,
        },
        _logger,
        _logged_usr_error,
        _logged_usr_truncation_warning
    );

    // Set dd.account, for 'account' on RUM events: extra attributes will be truncated
    // from the back as needed to fit
    TrySetAnnotationWithExtras(
        s_dd_account,
        AccountAnnotation{
            ctx.account_id,
            ctx.account_name,
            ctx.account_extra,
        },
        _logger,
        _logged_account_error,
        _logged_account_truncation_warning
    );

    // Set dd.rum.config: this ensures that if the handler needs to synthesize a new RUM
    // session, it can make the same sampling decision that the app would have made
    TrySetAnnotation(
        s_dd_rum_config,
        RumConfigAnnotation{
            ctx.rum_initial_config.application_id,
            ctx.rum_initial_config.session_sample_rate,
        },
        _logger,
        _logged_rum_config_error
    );

    // Set dd.rum.session, telling the handler whether the app had an active RUM session
    // when it crashed, and if so, what state that session was in: this allows the
    // handler to make decisions about how to create and/or modify RUM view events as
    // needed to record the crash as an error
    TrySetAnnotation(
        s_dd_rum_session,
        RumSessionAnnotation{
            ctx.rum_session_state.session_id,
            ctx.rum_session_state.is_sampled,
            ctx.rum_session_state.is_active,
            ctx.rum_session_state.is_initial_session,
            ctx.rum_session_state.has_tracked_any_view,
            ctx.rum_session_state.did_start_with_replay,
        },
        _logger,
        _logged_rum_session_error
    );

    // Set dd.rum.attributes, recording the last-known set of global RUM attributes so
    // that the handler can include an up-to-date set of attributes with the crash. If
    // encoding the full set of attributes as a JSON object would exceed the buffer
    // size, top-level properties will be dropped until it fits.
    TrySetAnnotationWithExtras(
        s_dd_rum_attributes,
        ctx.global_rum_attributes,
        _logger,
        _logged_rum_attributes_error,
        _logged_rum_attributes_truncation_warning
    );

    // Set dd.rum.last_view, recording the full RUM View event that was most recently
    // produced (or an empty object if no view is active), so that the handler can pull
    // attributes from that view event as needed to construct an accurate Error event,
    // and so it can produce a new event to update the state of the view to reflect the
    // crash. If the event doesn't fit, we'll drop top-level properties from the custom
    // attributes stored in `context`.
    TrySetLastViewAnnotation(
        s_dd_rum_last_view,
        ctx.last_view_event_json,
        _logger,
        _logged_rum_last_view_error,
        _logged_rum_last_view_truncation_warning
    );
  };

 private:
  DiagnosticLogger _logger;

  // Tracking consent cached on last call to SetCrashContext
  TrackingConsent _tracking_consent{TrackingConsent::Pending};

  // Once-per-process flags preventing repeated logging of the same encode failure
  bool _logged_config_error{false};
  bool _logged_os_error{false};
  bool _logged_device_error{false};
  bool _logged_usr_error{false};
  bool _logged_usr_truncation_warning{false};
  bool _logged_account_error{false};
  bool _logged_account_truncation_warning{false};
  bool _logged_rum_config_error{false};
  bool _logged_rum_session_error{false};
  bool _logged_rum_attributes_error{false};
  bool _logged_rum_attributes_truncation_warning{false};
  bool _logged_rum_last_view_error{false};
  bool _logged_rum_last_view_truncation_warning{false};
};

std::unique_ptr<ICrashHandler> CrashHandler::Create() {
  return std::make_unique<CrashpadCrashHandler>();
}

}  // namespace datadog::impl
