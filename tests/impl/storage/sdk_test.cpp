// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/storage/sdk.hpp"

#include <cstring>

#include "mock/filesystem_new.hpp"
#include "support/catch.hpp"
#include "support/diagnostics.hpp"

using namespace datadog::impl;

TEST_CASE("SdkStorage", "[unit][storage]") {
  MockFilesystem fs;
  fs.Mkdirs("my-app/storage");

  // TODO: Utility pls
  DiagnosticMessageBuffer diagnostics;
  DiagnosticLogger logger(
      [&](const datadog::DiagnosticMessage& message) {
        switch (message.level) {
          case datadog::DiagnosticLevel::Debug:
            diagnostics.debug.emplace_back(message.text);
            break;
          case datadog::DiagnosticLevel::Status:
            diagnostics.status.emplace_back(message.text);
            break;
          case datadog::DiagnosticLevel::Warning:
            diagnostics.warning.emplace_back(message.text);
            break;
          case datadog::DiagnosticLevel::Error:
            diagnostics.error.emplace_back(message.text);
            break;
        }
      },
      DiagnosticLevel::Debug
  );

  SECTION("M return empty string_view W Get is called on uninitialized") {
    fs.Mkdirs("my-app/storage");

    // Given a StoragePath value that is not explicitly populated
    {
      SdkStorage storage(fs, 12345);
      const bool ok = storage.Initialize(logger, "my-app/storage", "main");
      REQUIRE(ok);
      REQUIRE(fs.GetOpenHandles().size() == 1);
    }
    REQUIRE(fs.GetOpenHandles().size() == 0);
  }
}
