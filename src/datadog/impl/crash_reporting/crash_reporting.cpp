// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/crash_reporting/crash_reporting.hpp"

#include <memory>

#include "datadog/impl/core/feature_message.hpp"
#include "datadog/impl/core/platform/system_info.hpp"
#include "datadog/impl/core/storage/filesystem.hpp"
#include "datadog/impl/core/util/assert.hpp"
#include "datadog/impl/core/util/json.hpp"
#include "datadog/impl/crash_reporting/crash_processing.hpp"

namespace datadog::impl {

template <typename T>
static void encode_json_to_string(std::string& out, const T& value) {
  const size_t n = GetJsonSize(value);
  out.resize(n);
  out.resize(WriteJson(out.data(), n, value));
}

CrashReporting::CrashReporting(
    ICrashHandler& handler, IFilesystem& fs, const StoragePath& crash_storage_dir_path
)
    : _handler(handler), _fs(fs) {
  _crash_storage_dir_path.MustSet(crash_storage_dir_path);
}

std::optional<Report> CrashReporting::UploadThread_PrepareReport(
    BatchReader& reader, HttpRequestBuilder& builder
) {
  // The Crash Reporting feature implementation does not currently generate or upload
  // any events in-process
  (void)reader;
  (void)builder;
  return std::nullopt;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
std::optional<std::function<void(const FeatureMessage&)>>
CrashReporting::MakeMessageHandler() {
  // Bind a weak_ptr to this so the callback will silently no-op after we're destroyed
  const auto weak_self = weak_from_this();
  return [weak_self](const FeatureMessage& msg) {
    // Abort if our weak_ptr is no longer valid
    auto self = std::static_pointer_cast<CrashReporting>(weak_self.lock());
    if (!self) {
      return;
    }

    CrashContext& cc = self->_crash_context;

    // MSVC flags each `else if (const auto* m = ...)` branch as shadowing an outer `m`,
    // even though the scopes are mutually exclusive
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4456)
#endif
    if (const auto* m = std::get_if<ContextChangedMessage>(&msg)) {
      // Copy SDK identity and platform info from the CoreContext snapshot into the
      // accumulated CrashContext. The os/device pointers are guaranteed non-null while
      // the Core is running.
      const CoreContext& ctx = m->context;
      cc.service = ctx.service;
      cc.env = ctx.env;
      cc.application_version = ctx.application_version;
      cc.variant = ctx.variant;
      cc.source = ctx.source;
      cc.sdk_version = ctx.sdk_version;
      cc.tracking_consent = ctx.tracking_consent;
      cc.os_name = ctx.os->name;
      cc.os_version = ctx.os->version;
      cc.os_build = ctx.os->build;
      cc.os_version_major = ctx.os->version_major;
      cc.device_type = ctx.device->type;
      cc.device_name = ctx.device->name;
      cc.device_model = ctx.device->model;
      cc.device_brand = ctx.device->brand;
      cc.device_architecture = ctx.device->architecture;
      cc.device_locale = ctx.device->locale;
      cc.device_time_zone = ctx.device->time_zone;
      cc.user_id = ctx.user_info.id;
      cc.user_name = ctx.user_info.name;
      cc.user_email = ctx.user_info.email;
      cc.user_extra = ctx.user_info.extra;
      cc.user_anonymous_id = ctx.anonymous_id_enabled ? ctx.anonymous_id : UUID::Zero;
      cc.account_id = ctx.account_info.id;
      cc.account_name = ctx.account_info.name;
      cc.account_extra = ctx.account_info.extra;
    } else if (const auto* m = std::get_if<RumInitializedMessage>(&msg)) {
      cc.rum_initial_config = m->initial_config;
    } else if (const auto* m = std::get_if<RumSessionStateChangedMessage>(&msg)) {
      cc.rum_session_state = m->session_state;
    } else if (const auto* m = std::get_if<RumActiveViewUpdatedMessage>(&msg)) {
      encode_json_to_string(cc.last_view_event_json, m->view_event);
    } else if (std::get_if<RumActiveViewLostMessage>(&msg)) {
      cc.last_view_event_json.clear();
    } else if (const auto* m = std::get_if<RumGlobalAttributesChangedMessage>(&msg)) {
      cc.global_rum_attributes = m->attributes;
    } else {
      // CrashReportProcessedMessage does not affect crash context; no action needed
      return;
    }

#ifdef _MSC_VER
#pragma warning(pop)
#endif
    self->_handler.SetCrashContext(self->_fs, cc);
  };
}

void CrashReporting::Start() {
  // We should always have a valid FeatureScope on SDK start
  if (!_scope) {
    DATADOG_ASSERT(_scope, "CrashReporting has invalid _scope on Start()");
    return;
  }
  FeatureScope& scope = *_scope;

  // Enqueue ProcessCrashReports() to be run on the context thread, causing all
  // available crash reports in <application-root>/.datadog/.crashes/ to be processed
  // asynchronously
  const auto weak_self = weak_from_this();
  scope.ExecuteOnContextThread(
      [weak_self](
          const CoreContext&, const EventWriter&, const MessagePublisher& publisher
      ) {
        // If SDK was stopped or destroyed before our context-thread function ran, abort
        auto self = std::static_pointer_cast<CrashReporting>(weak_self.lock());
        if (!self || !self->_scope) {
          return;
        }

        // Process all crash reports, publishing a `CrashReportProcessedMessage` for
        // each crash we can successfully parse
        ProcessCrashReports(
            self->_scope->diagnostic_logger,
            self->_fs,
            self->_crash_storage_dir_path,
            [&publisher](CrashReport crash) {
              return publisher(CrashReportProcessedMessage{std::move(crash)});
            }
        );
      }
  );
}

}  // namespace datadog::impl
