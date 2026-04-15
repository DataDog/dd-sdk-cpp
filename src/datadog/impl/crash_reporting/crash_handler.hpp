// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <memory>
#include <string_view>

namespace datadog::impl {
class DiagnosticLogger;
struct RumFeatureContext;
}  // namespace datadog::impl

namespace datadog::impl {

/**
 * Abstract interface for detecting and handling crashes in the process where the SDK
 * resides.
 *
 * When an application developer wants to enable crash reporting via the Datadog SDK,
 * they enable the CrashReporting feature, either via the `datadog::CrashReporting`
 * interface (C++ API), or the `dd_crash_reporting_t` interface (C API). This API-layer
 * object wraps an instance of `datadog::impl::CrashReporting`, the **feature
 * implementation**.
 *
 * The feature implementation uses an instance of `impl::ICrashHandler` to facilitate
 * facilitate key operations like initializing and uninitializing crash-handler state,
 * updating metadata to be included in crash reports, etc.; thereby allowing the Crash
 * Reporting API to be used identically with a variety of underlying crash-handler
 * implementations.
 *
 * Note that the exact behavior of Crash Reporting therefore depends on two separate
 * factors:
 *
 * 1. Whether the CrashReporting feature is present in the SDK binary _and_ explicitly
 *    registered by the application as an SDK feature.
 *
 *    - At present, all features are included in the SDK unconditionally, within a
 *      single binary.
 *    - An application developer can effectively make CrashReporting inert (no runtime
 *      overhead even if called; no impact on binary size) by building the SDK from
 *      source with DD_CRASH_MODE=noop.
 *    - TODO(RUM-11669): Update as needed if features are modularized
 *
 * 2. Which ICrashHandler implementation was enabled when the SDK was compiled.
 *
 *    - This is controlled via the DD_CRASH_MODE CMake var; e.g. DD_CRASH_MODE=inprocess
 */
class ICrashHandler {
 protected:
  ICrashHandler() = default;

 public:
  virtual ~ICrashHandler() = default;
  ICrashHandler(const ICrashHandler&) = delete;
  ICrashHandler& operator=(const ICrashHandler&) = delete;
  ICrashHandler(ICrashHandler&&) = default;
  ICrashHandler& operator=(ICrashHandler&&) = default;

  /**
   * Initializes the crash handler, performing any setup required for it to detect and
   * handle crashes.
   */
  virtual bool Initialize() = 0;

  /**
   * Makes a best-effort attempt to reverse the setup performed in a successful call to
   * Initialize(), such that crashes will no longer be handled by this implementation,
   * and will instead be handled as configured prior to the call to Initialize().
   *
   * Some crash handler implementations may not be fully reversible. The SDK may not
   * assume that Initialize() is safe to call more than once, even if Shutdown() is
   * called.
   *
   * TODO(RUM-14021): Make explicit guarantees about exactly when the SDK will or won't
   *  call both `CrashHandler::Init` (i.e. how many ICrashHandler instances it will
   *  create in the lifetime of a single process) and `ICrashHandler::Initialize` (i.e.
   *  how many init/shutdown cycles it will initiate within the lifetime of a single
   *  `ICrashHandler` instance). The safest answer is "no more than once" for both, but
   *  we may want to make this configurable per-implementation by providing a function
   *  like `virtual bool IsProcessGlobal()` or `IsReversible()` etc.
   */
  virtual void Shutdown() = 0;

  /**
   * Persists `rum_ctx` so that it is available if a crash occurs before the next
   * normal SDK shutdown. Only called when the RUM context has actually changed since
   * the last call.
   */
  virtual void SetRumContext(const RumFeatureContext& rum_ctx) = 0;
};

namespace CrashHandler {
/**
 * Initializes and returns a new ICrashHandler instance to be used by the SDK's
 * CrashReporting feature. May return null if unable to initialize the handler.
 *
 * TODO(RUM-14021): Strictly establish whether ICrashHandler is assumed to be
 *  process-wide; whether initialization may or must be reversible; whether the SDK will
 *  automatically handle the multiple-SDK-instances-with-crash-reporting case and
 *  refrain from calling CrashHandler::Init() after the first time a handler is
 *  successfully initialized.
 *
 * @param logger - Logger for local diagnostic messages; may be used to emit warnings
 *  and errors to provide more information on implementation-specific failures. May be
 *  stored persistently.
 * @param handler_exe_path - Path provided by the application in Crash Reporting config.
 *  If the crash handler implementation requires launching an external process, this
 *  value may be used to override the default path for that executable. For other crash
 *  handler implementations, this value is irrelevant and may be ignored.
 */
std::unique_ptr<ICrashHandler> Init(
    DiagnosticLogger& logger, std::string_view handler_exe_path
);
}  // namespace CrashHandler

}  // namespace datadog::impl
