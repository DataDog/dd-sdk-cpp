// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/storage/artifact.hpp"

#include "datadog/impl/storage/sdk.hpp"

#include "mock/filesystem_new.hpp"
#include "support/catch.hpp"
#include "support/diagnostics.hpp"

using namespace datadog::impl;

TEST_CASE("ArtifactStorage paths", "[unit][storage]") {
  // Given a platform-appropriate root application storage path, and the expected prefix
  // for our artifact storage directory
#ifdef _WIN32
  std::string root = "C:/Users/jqpublic/AppData/Local/someapp";
  std::string want_path = root + "\\.datadog\\.somestuff";
#else
  std::string root = "/home/jqpublic/.local/share/someapp";
  std::string want_path = root + "/.datadog/.somestuff";
#endif

  // And a successfully-initialized a ArtifactStorage instance within that root dir
  MockFilesystemNew fs;
  fs.Mkdirs(root);
  DiagnosticMessageBuffer diagnostics;
  DiagnosticLogger logger = diagnostics.CreateTestLogger();
  SdkStorage storage(fs, logger, 555);
  REQUIRE(storage.Initialize(root, "main"));
  auto artifacts = storage.InitializeArtifactStorage(".somestuff");
  REQUIRE(artifacts != nullptr);

  SECTION("M return valid directory path W GetPath called") {
    // When we call GetPath()
    auto path = artifacts->GetPath();

    // Then we get a valid path, relative to our root storage directory, to the
    // directory where we're permitted to store arbitrary files
    REQUIRE(path.Get() == want_path);
  }
}
