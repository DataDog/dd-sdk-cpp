// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <memory>
#include <string_view>

#include "datadog/impl/core/feature.hpp"
#include "datadog/impl/core/feature_types/crash_reporting.hpp"
#include "datadog/impl/core/platform/clock.hpp"
#include "datadog/impl/core/storage/path.hpp"
#include "datadog/impl/crash_reporting/crash_handler.hpp"

namespace datadog::impl {

class IFilesystem;

/**
 * Crash Reporting feature implementation.
 *
 * The `CrashReporting` instance works in conjunction with an `ICrashHandler`, which is
 * responsible for the low-level details of detecting and responding to crashes. The
 * feature implementation has two key responsibilities:
 *
 * 1. Conveying changes in SDK state to the handler, so that details like current RUM
 *    context, user information, tracking consent state, etc., can be incorporated into
 *    the crash reports that the handler produces.
 *
 * 2. Scanning <application-storage>/.datadog/.crashes/ on startup to find binary crash
 *    reports left behind by previous application processes, then claiming any such
 *    files, parsing them, generating RUM Errors, and uploading those errors.
 *
 * This scanning-and-upload behavior is decoupled from the handler implementation: not
 * all handlers write binary crash dumps to .crashes/, but the SDK will look for files
 * in .crashes/ regardless.
 *
 * == FEATURE AND HANDLER LIFETIME ==
 *
 * Only one instance of `CrashReporting` will be created in any given process, and that
 * single instance is solely responsible for uploading crash reports and interoperating
 * with the handler. The handler itself is a process-global singleton.
 *
 * When an application registers the crash reporting feature for the first time, the API
 * will create and initialize a single, process-global `ICrashHandler` instance, then
 * create an instance of `CrashReporting` initialized with a reference to that handler.
 *
 * If the application attempts to enable crash reporting on any SDK instance (`Core`)
 * beyond the first, the API will return a no-op handle rather than creating another
 * handler or another `impl::CrashReporting` instance.
 */
class CrashReporting final : public Feature {
 public:
  explicit CrashReporting(
      ICrashHandler& handler, IFilesystem& fs, const StoragePath& crash_storage_dir_path
  );

  // Feature interface
  FeatureId GetId() const override { return CreateFeatureId("CRSH"); }

  std::string_view GetName() const override { return "crash_reporting"; }

  std::optional<Report> UploadThread_PrepareReport(
      BatchReader& reader, RequestBuilder& builder
  ) override;

  std::optional<std::function<void(const FeatureMessage&)>>
  MakeMessageHandler() override;

 protected:
  void Start() override;

 private:
  // Reference to the process-global crash handler implementation
  ICrashHandler& _handler;

  // Filesystem interface used to scan for crash reports, and provided to the handler
  // when context is is propagated via ICrashHandler::SetRumContext() et al.
  IFilesystem& _fs;

  // Path to the directory where crash reports may be stored
  StoragePath _crash_storage_dir_path;

  // Accumulated crash context, updated incrementally as SDK state changes
  CrashContext _crash_context;
};

}  // namespace datadog::impl
