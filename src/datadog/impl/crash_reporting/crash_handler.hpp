// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <memory>
#include <string_view>

#include "datadog/impl/core/feature_types/crash_reporting.hpp"
#include "datadog/impl/core/util/diagnostics.hpp"

namespace datadog::impl {

class IFilesystem;
class StoragePath;

/**
 * Abstract interface for detecting and handling crashes in the process where the SDK
 * resides.
 *
 * The handler is responsible for the low-level mechanics of detecting and responding to
 * crashes. Once intialized, it remains alive indefinitely for as long as the process
 * lives, potentially outliving the SDK instance responsible for its creation. As such,
 * ICrashHandler has no direct references to any SDK state.
 *
 * Meanwhile, the `impl::CrashReporting` feature implementation is responsible for
 * conveying application context to the handler, and for parsing and uploading crash
 * dump files detected on SDK startup. The first `CrashReporting` instance that's
 * registered will be initialized with a reference to the handler, and may push new data
 * to it (via `SetRumContext` etc.) for as long as the associated SDK instance is
 * active.
 */
class ICrashHandler {
 protected:
  ICrashHandler() = default;

 public:
  /**
   * Initializes the global crash handler, performing any setup required for it to
   * detect and handle crashes. Returns success.
   *
   * This function will be called no more than once per process.
   */
  virtual bool Initialize(
      DiagnosticLogger logger,
      IFilesystem& fs,
      const StoragePath& crash_storage_dir_path,
      std::string_view helper_exe_path,
      std::string_view upload_origin
  ) = 0;

  /**
   * Allows the handler to perform any required cleanup in cases where no crash occurred
   * during its lifetime.
   */
  virtual ~ICrashHandler() = default;

  // Noncopyable, nonmovable
  ICrashHandler(const ICrashHandler&) = delete;
  ICrashHandler& operator=(const ICrashHandler&) = delete;
  ICrashHandler(ICrashHandler&&) = delete;
  ICrashHandler& operator=(ICrashHandler&&) = delete;

  /**
   * Persists `ctx` so that it is available if a crash occurs before the next normal SDK
   * shutdown. Called whenever any field of the accumulated crash context changes.
   */
  virtual void SetCrashContext(IFilesystem& fs, const CrashContext& ctx) = 0;
};

namespace CrashHandler {
/**
 * Constructs a new instance of the ICrashHandler implementation type that's been linked
 * into this SDK build.
 */
std::unique_ptr<ICrashHandler> Create();
}  // namespace CrashHandler

}  // namespace datadog::impl
