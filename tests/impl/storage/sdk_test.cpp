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

namespace {

/**
 * Helper function to create a mock abandoned directory with the expected
 * structure: <root>/.datadog/<pid>/<instance>/<feature>/{v1,intermediate-v1}/
 *
 * If `create_lockfile` is true, also creates the lockfile at
 * <root>/.datadog/<pid>.lock
 */
void CreateAbandonedDirectory(
    MockFilesystem& fs,
    std::string_view root,
    std::string_view pid,
    std::string_view instance,
    std::string_view feature,
    bool create_lockfile = true
) {
  std::string base = std::string(root) + "/.datadog/" + std::string(pid) + "/" +
                     std::string(instance) + "/" + std::string(feature);
  fs.Mkdirs(base + "/v1");
  fs.Mkdirs(base + "/intermediate-v1");

  if (create_lockfile) {
    std::string lockfile =
        std::string(root) + "/.datadog/" + std::string(pid) + ".lock";
    fs.Touch(lockfile);
  }
}

/**
 * Helper function to write a test event file in an abandoned directory.
 */
void WriteTestEvent(
    MockFilesystem& fs,
    std::string_view root,
    std::string_view pid,
    std::string_view instance,
    std::string_view feature,
    std::string_view subdir,
    std::string_view filename,
    std::string_view content
) {
  std::string path = std::string(root) + "/.datadog/" + std::string(pid) + "/" +
                     std::string(instance) + "/" + std::string(feature) + "/" +
                     std::string(subdir) + "/" + std::string(filename);
  fs.Touch(path, content);
}

/**
 * Helper function to verify an event file exists and has the expected content.
 */
bool VerifyEventFile(
    MockFilesystem& fs,
    std::string_view root,
    std::string_view pid,
    std::string_view instance,
    std::string_view feature,
    std::string_view subdir,
    std::string_view filename,
    std::string_view expected_content
) {
  std::string path = std::string(root) + "/.datadog/" + std::string(pid) + "/" +
                     std::string(instance) + "/" + std::string(feature) + "/" +
                     std::string(subdir) + "/" + std::string(filename);
  try {
    std::string content = fs.Cat(path);
    return content == expected_content;
  } catch (...) {
    return false;
  }
}

/**
 * Helper function to check if a directory exists.
 */
bool DirectoryExists(MockFilesystem& fs, std::string_view path) {
  PlatformPath platform_path;
  std::string path_str(path);
  if (!platform_path.Encode(path_str.c_str())) {
    return false;
  }
  std::vector<std::string> subdirs;
  return fs.ListSubdirectories(platform_path, subdirs) == FilesystemResult::OK;
}

/**
 * Helper function to check if a file exists.
 */
bool FileExists(MockFilesystem& fs, std::string_view path) {
  try {
    fs.Cat(std::string(path));
    return true;
  } catch (...) {
    return false;
  }
}

/**
 * Helper function to count subdirectories in a directory.
 */
int CountSubdirectories(MockFilesystem& fs, std::string_view path) {
  PlatformPath platform_path;
  std::string path_str(path);
  if (!platform_path.Encode(path_str.c_str())) {
    return -1;
  }
  std::vector<std::string> subdirs;
  if (fs.ListSubdirectories(platform_path, subdirs) != FilesystemResult::OK) {
    return -1;
  }
  return static_cast<int>(subdirs.size());
}

}  // namespace

TEST_CASE("SdkStorage", "[unit][storage]") {
  MockFilesystem fs;
  fs.Mkdirs("my-app/storage");

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

    {
      SdkStorage storage(fs, 12345);
      const bool ok = storage.Initialize(logger, "my-app/storage", "main");
      REQUIRE(ok);
      REQUIRE(fs.GetOpenHandles().size() == 1);
    }
    REQUIRE(fs.GetOpenHandles().size() == 0);
  }

  SECTION("creates new directory structure when no abandoned directories exist") {
    SdkStorage storage(fs, 12345);
    REQUIRE(storage.Initialize(logger, "my-app/storage", "main"));

    REQUIRE(FileExists(fs, "my-app/storage/.datadog/12345.lock"));

    REQUIRE(DirectoryExists(fs, "my-app/storage/.datadog/12345"));
    REQUIRE(DirectoryExists(fs, "my-app/storage/.datadog/12345/main"));

    int dir_count = CountSubdirectories(fs, "my-app/storage/.datadog");
    REQUIRE(dir_count == 1);
  }

  SECTION("reuses abandoned directory when process is dead (rename optimization)") {
    CreateAbandonedDirectory(fs, "my-app/storage", "99999", "main", "rum", true);
    WriteTestEvent(
        fs,
        "my-app/storage",
        "99999",
        "main",
        "rum",
        "v1",
        "event1.json",
        "{\"id\": 1, \"type\": \"view\"}"
    );
    WriteTestEvent(
        fs,
        "my-app/storage",
        "99999",
        "main",
        "rum",
        "intermediate-v1",
        "event2.json",
        "{\"id\": 2, \"type\": \"action\"}"
    );

    SdkStorage storage(fs, 12345);
    REQUIRE(storage.Initialize(logger, "my-app/storage", "main"));

    // Directory was renamed atomically from 99999 to 12345
    REQUIRE(!DirectoryExists(fs, "my-app/storage/.datadog/99999"));
    REQUIRE(DirectoryExists(fs, "my-app/storage/.datadog/12345"));

    // New lockfile created
    REQUIRE(FileExists(fs, "my-app/storage/.datadog/12345.lock"));

    // Events preserved in renamed directory
    REQUIRE(VerifyEventFile(
        fs,
        "my-app/storage",
        "12345",
        "main",
        "rum",
        "v1",
        "event1.json",
        "{\"id\": 1, \"type\": \"view\"}"
    ));
    REQUIRE(VerifyEventFile(
        fs,
        "my-app/storage",
        "12345",
        "main",
        "rum",
        "intermediate-v1",
        "event2.json",
        "{\"id\": 2, \"type\": \"action\"}"
    ));
  }

  SECTION("migrates events from multiple abandoned directories") {
    CreateAbandonedDirectory(fs, "my-app/storage", "88888", "main", "rum", true);
    WriteTestEvent(
        fs,
        "my-app/storage",
        "88888",
        "main",
        "rum",
        "v1",
        "event1.json",
        "{\"id\": 1, \"source\": \"88888\"}"
    );

    CreateAbandonedDirectory(fs, "my-app/storage", "99999", "main", "logs", true);
    WriteTestEvent(
        fs,
        "my-app/storage",
        "99999",
        "main",
        "logs",
        "v1",
        "event2.json",
        "{\"id\": 2, \"source\": \"99999\"}"
    );

    CreateAbandonedDirectory(fs, "my-app/storage", "77777", "main", "rum", true);
    WriteTestEvent(
        fs,
        "my-app/storage",
        "77777",
        "main",
        "rum",
        "intermediate-v1",
        "event3.json",
        "{\"id\": 3, \"source\": \"77777\"}"
    );

    SdkStorage storage(fs, 12345);
    REQUIRE(storage.Initialize(logger, "my-app/storage", "main"));

    // One directory gets renamed (disappears), others remain but files are migrated
    REQUIRE(DirectoryExists(fs, "my-app/storage/.datadog/12345"));
    int renamed_count = 0;
    if (!DirectoryExists(fs, "my-app/storage/.datadog/88888")) renamed_count++;
    if (!DirectoryExists(fs, "my-app/storage/.datadog/99999")) renamed_count++;
    if (!DirectoryExists(fs, "my-app/storage/.datadog/77777")) renamed_count++;
    REQUIRE(renamed_count == 1);  // Exactly one directory was renamed

    // New lockfile created
    REQUIRE(FileExists(fs, "my-app/storage/.datadog/12345.lock"));

    // All events migrated to 12345
    REQUIRE(VerifyEventFile(
        fs,
        "my-app/storage",
        "12345",
        "main",
        "rum",
        "v1",
        "event1.json",
        "{\"id\": 1, \"source\": \"88888\"}"
    ));
    REQUIRE(VerifyEventFile(
        fs,
        "my-app/storage",
        "12345",
        "main",
        "logs",
        "v1",
        "event2.json",
        "{\"id\": 2, \"source\": \"99999\"}"
    ));
    REQUIRE(VerifyEventFile(
        fs,
        "my-app/storage",
        "12345",
        "main",
        "rum",
        "intermediate-v1",
        "event3.json",
        "{\"id\": 3, \"source\": \"77777\"}"
    ));
  }

  SECTION("skips non-numeric directories during migration scan") {
    CreateAbandonedDirectory(fs, "my-app/storage", "not-a-pid", "main", "rum", true);
    WriteTestEvent(
        fs,
        "my-app/storage",
        "not-a-pid",
        "main",
        "rum",
        "v1",
        "event.json",
        "{\"id\": 1}"
    );

    SdkStorage storage(fs, 12345);
    REQUIRE(storage.Initialize(logger, "my-app/storage", "main"));

    // Both directories should exist (non-numeric dir not claimed)
    REQUIRE(DirectoryExists(fs, "my-app/storage/.datadog/not-a-pid"));
    REQUIRE(DirectoryExists(fs, "my-app/storage/.datadog/12345"));

    // Non-numeric directory's lockfile and events should remain untouched
    REQUIRE(FileExists(fs, "my-app/storage/.datadog/not-a-pid.lock"));
    REQUIRE(VerifyEventFile(
        fs,
        "my-app/storage",
        "not-a-pid",
        "main",
        "rum",
        "v1",
        "event.json",
        "{\"id\": 1}"
    ));
  }

  SECTION("migrates events from multiple instances and features") {
    CreateAbandonedDirectory(fs, "my-app/storage", "99999", "main", "rum", true);
    CreateAbandonedDirectory(fs, "my-app/storage", "99999", "main", "logs", false);
    CreateAbandonedDirectory(fs, "my-app/storage", "99999", "worker", "rum", false);

    WriteTestEvent(
        fs, "my-app/storage", "99999", "main", "rum", "v1", "event1.json", "{\"id\": 1}"
    );
    WriteTestEvent(
        fs,
        "my-app/storage",
        "99999",
        "main",
        "logs",
        "v1",
        "event2.json",
        "{\"id\": 2}"
    );
    WriteTestEvent(
        fs,
        "my-app/storage",
        "99999",
        "worker",
        "rum",
        "v1",
        "event3.json",
        "{\"id\": 3}"
    );

    SdkStorage storage(fs, 12345);
    REQUIRE(storage.Initialize(logger, "my-app/storage", "main"));

    // Directory was renamed atomically, preserving all instances/features
    REQUIRE(!DirectoryExists(fs, "my-app/storage/.datadog/99999"));
    REQUIRE(DirectoryExists(fs, "my-app/storage/.datadog/12345"));

    // All events migrated to 12345, structure preserved
    REQUIRE(VerifyEventFile(
        fs, "my-app/storage", "12345", "main", "rum", "v1", "event1.json", "{\"id\": 1}"
    ));
    REQUIRE(VerifyEventFile(
        fs,
        "my-app/storage",
        "12345",
        "main",
        "logs",
        "v1",
        "event2.json",
        "{\"id\": 2}"
    ));
    REQUIRE(VerifyEventFile(
        fs,
        "my-app/storage",
        "12345",
        "worker",
        "rum",
        "v1",
        "event3.json",
        "{\"id\": 3}"
    ));
  }

  SECTION("skips abandoned directories without lockfiles") {
    CreateAbandonedDirectory(fs, "my-app/storage", "99999", "main", "rum", false);
    WriteTestEvent(
        fs, "my-app/storage", "99999", "main", "rum", "v1", "event.json", "{\"id\": 1}"
    );

    SdkStorage storage(fs, 12345);
    REQUIRE(storage.Initialize(logger, "my-app/storage", "main"));

    // Both directories should exist (no lockfile means can't verify process death)
    REQUIRE(DirectoryExists(fs, "my-app/storage/.datadog/99999"));
    REQUIRE(DirectoryExists(fs, "my-app/storage/.datadog/12345"));

    // Events remain in original directory, not migrated
    REQUIRE(VerifyEventFile(
        fs, "my-app/storage", "99999", "main", "rum", "v1", "event.json", "{\"id\": 1}"
    ));
  }

  SECTION("handles empty abandoned directories") {
    CreateAbandonedDirectory(fs, "my-app/storage", "99999", "main", "rum", true);

    SdkStorage storage(fs, 12345);
    REQUIRE(storage.Initialize(logger, "my-app/storage", "main"));

    // Empty directory was renamed atomically from 99999 to 12345
    REQUIRE(!DirectoryExists(fs, "my-app/storage/.datadog/99999"));
    REQUIRE(DirectoryExists(fs, "my-app/storage/.datadog/12345"));

    // New lockfile created
    REQUIRE(FileExists(fs, "my-app/storage/.datadog/12345.lock"));
  }
}
