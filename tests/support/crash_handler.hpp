// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <optional>
#include <string_view>

#include "datadog/impl/core/feature_types/crash_reporting.hpp"
#include "datadog/impl/crash_reporting/crash_handler.hpp"
#include "datadog/impl/types/diagnostics.hpp"

using namespace datadog;

class MockCrashHandler : public impl::ICrashHandler {
 public:
  size_t num_set_crash_context_calls{0};
  std::optional<impl::CrashContext> last_crash_ctx;

  // ICrashHandler interface
  bool Initialize(
      impl::DiagnosticLogger logger,
      impl::IFilesystem& fs,
      const impl::StoragePath& crash_storage_dir_path,
      std::string_view helper_exe_path,
      std::string_view upload_origin
  ) override {
    (void)logger;
    (void)fs;
    (void)crash_storage_dir_path;
    (void)helper_exe_path;
    (void)upload_origin;
    return true;
  }

  void SetCrashContext(impl::IFilesystem& fs, const impl::CrashContext& ctx) override {
    (void)fs;
    num_set_crash_context_calls++;
    last_crash_ctx = ctx;
  };
};
