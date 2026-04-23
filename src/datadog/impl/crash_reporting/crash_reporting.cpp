// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/crash_reporting/crash_reporting.hpp"

#include <memory>

#include "datadog/impl/core/feature_message.hpp"
#include "datadog/impl/core/storage/filesystem.hpp"
#include "datadog/impl/core/util/assert.hpp"
#include "datadog/impl/core/writer.hpp"
#include "datadog/impl/crash_reporting/crash_processing.hpp"

namespace datadog::impl {

CrashReporting::CrashReporting(
    ICrashHandler& handler, IFilesystem& fs, const StoragePath& crash_storage_dir_path
)
    : _handler(handler), _fs(fs) {
  _crash_storage_dir_path.MustSet(crash_storage_dir_path);
}

std::optional<Report> CrashReporting::UploadThread_PrepareReport(
    const HttpContext& context, BatchReader& reader
) {
  // The Crash Reporting feature implementation does not currently generate or upload
  // any events in-process
  (void)context;
  (void)reader;
  return std::nullopt;
}

std::optional<std::function<void(const FeatureMessage&)>>
CrashReporting::MakeMessageHandler() {
  // Bind a weak_ptr to this so the callback will silently no-op after we're destroyed
  const auto weak_self = weak_from_this();
  return [weak_self](const FeatureMessage& msg) {
    // Handle ContextChangedMessage by conveying the latest RUM context to the handler
    const auto* context_changed = std::get_if<ContextChangedMessage>(&msg);
    if (!context_changed || !context_changed->context.rum) {
      return;
    }

    // Abort if our weak_ptr is no longer valid: if the messaging thread is still
    // running after our feature is destroyed, we should silently drop the message
    auto self = std::static_pointer_cast<CrashReporting>(weak_self.lock());
    if (!self) {
      return;
    }

    // Only notify the handler if RUM context has actually changed since last time
    const auto& rum_ctx = *context_changed->context.rum;
    if (self->_last_rum_ctx == rum_ctx) {
      return;
    }
    self->_last_rum_ctx = rum_ctx;
    self->_handler.SetRumContext(self->_fs, rum_ctx);
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
