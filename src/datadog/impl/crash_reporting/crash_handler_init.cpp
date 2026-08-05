// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/crash_reporting/crash_handler_init.hpp"

#include <memory>
#include <mutex>

namespace datadog::impl {

ICrashHandler* CrashHandler::InitializeOnce(
    DiagnosticLogger logger,
    IFilesystem& fs,
    const StoragePath& crash_storage_dir_path,
    std::string_view helper_exe_path,
    std::string_view upload_origin
) {
  // Establish an ICrashHandler singleton: once initialized, this value will live for
  // the lifetime of the process, entirely decoupled from any SDK state. Upon a clean
  // process exit, ~ICrashHandler() will be invoked.
  static std::unique_ptr<ICrashHandler> s_handler;

  // Result value: remains null except on the first call, if initialization succeeds
  ICrashHandler* result = nullptr;

  // Make exactly one attempt to create and initialize an ICrashHandler implementation,
  // the first time this function is called
  static std::once_flag s_flag;
  std::call_once(s_flag, [&] {
    // Construct an ICrashHandler instance using the implementation that's been linked
    // into this SDK build
    std::unique_ptr<ICrashHandler> new_handler = CrashHandler::Create();
    if (!new_handler) {
      logger.Error(
          "Failed to initialize crash handler: CrashHandler::Create returned no value"
      );
      return;
    }

    // Initialize the handler: this is the point where we actually register signal
    // handlers, launch helper processes, etc.
    if (!new_handler->Initialize(
            logger, fs, crash_storage_dir_path, helper_exe_path, upload_origin
        )) {
      return;
    }

    // Install our handler into the process-global unique_ptr, and populate the result
    // value so we'll return a valid ICrashHandler* to the caller
    s_handler = std::move(new_handler);
    result = s_handler.get();
  });

  // On successful init, return a raw pointer to the global ICrashHandler*, allowing it
  // to be installed into a CrashReporting feature implementation, endowing that
  // CrashReporting instance with the sole responsibility for handling crashes. In all
  // other cases, return null, indicating that any CrashReporting instance being
  // initialized should remain inert.
  return result;
}

}  // namespace datadog::impl
