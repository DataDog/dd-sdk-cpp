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

  SECTION("M close lockfile handle W SdkStorage is destroyed") {
    // Given a storage instance that has been initialized (acquiring a lockfile handle)
    fs.Mkdirs("my-app/storage");

    {
      SdkStorage storage(fs, 12345);
      const bool ok = storage.Initialize(logger, "my-app/storage", "main");
      REQUIRE(ok);

      // When the lockfile handle is open
      REQUIRE(fs.GetOpenHandles().size() == 1);
    }

    // Then the handle is closed on destruction
    REQUIRE(fs.GetOpenHandles().size() == 0);
  }

  SECTION("M create new directory structure W no abandoned directories exist") {
    // Given no pre-existing .datadog directories
    SdkStorage storage(fs, 12345);

    // When Initialize is called
    REQUIRE(storage.Initialize(logger, "my-app/storage", "main"));

    // Then the lockfile is created
    REQUIRE(FileExists(fs, "my-app/storage/.datadog/12345.lock"));

    // And the per-process directory and instance subdirectory are created
    REQUIRE(DirectoryExists(fs, "my-app/storage/.datadog/12345"));
    REQUIRE(DirectoryExists(fs, "my-app/storage/.datadog/12345/main"));

    // And no other subdirectories were created under .datadog
    int dir_count = CountSubdirectories(fs, "my-app/storage/.datadog");
    REQUIRE(dir_count == 1);

    // And GetEventsRoot returns the path to the instance subdirectory
    REQUIRE(storage.GetEventsRoot() == "my-app/storage/.datadog/12345/main");
  }

  SECTION("M reuse abandoned directory via rename W process is dead") {
    // Given an abandoned directory for a dead process with events in both v1 and
    // intermediate-v1, and a lockfile that can be acquired (process is dead)
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

    // When Initialize is called
    SdkStorage storage(fs, 12345);
    REQUIRE(storage.Initialize(logger, "my-app/storage", "main"));

    // Then the abandoned directory is renamed atomically to our PID
    REQUIRE(!DirectoryExists(fs, "my-app/storage/.datadog/99999"));
    REQUIRE(DirectoryExists(fs, "my-app/storage/.datadog/12345"));

    // And our lockfile is created
    REQUIRE(FileExists(fs, "my-app/storage/.datadog/12345.lock"));

    // And the events are preserved under the renamed directory
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

  SECTION("M migrate events from all abandoned directories W multiple exist") {
    // Given three abandoned directories for dead processes, each with events
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

    // When Initialize is called
    SdkStorage storage(fs, 12345);
    REQUIRE(storage.Initialize(logger, "my-app/storage", "main"));

    // Then all abandoned directories are removed: one via atomic rename, the rest
    // via file-by-file migration followed by recursive deletion
    REQUIRE(DirectoryExists(fs, "my-app/storage/.datadog/12345"));
    REQUIRE(!DirectoryExists(fs, "my-app/storage/.datadog/88888"));
    REQUIRE(!DirectoryExists(fs, "my-app/storage/.datadog/99999"));
    REQUIRE(!DirectoryExists(fs, "my-app/storage/.datadog/77777"));

    // And our lockfile is created
    REQUIRE(FileExists(fs, "my-app/storage/.datadog/12345.lock"));

    // And all events are migrated to our directory
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

  SECTION("M skip non-numeric directories W scanning for abandoned directories") {
    // Given a directory whose name is not a valid PID (cannot be an abandoned process)
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

    // When Initialize is called
    SdkStorage storage(fs, 12345);
    REQUIRE(storage.Initialize(logger, "my-app/storage", "main"));

    // Then the non-numeric directory is left untouched
    REQUIRE(DirectoryExists(fs, "my-app/storage/.datadog/not-a-pid"));
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

    // And our new directory is created normally
    REQUIRE(DirectoryExists(fs, "my-app/storage/.datadog/12345"));
  }

  SECTION(
      "M migrate events from all instances and features W directory claimed via rename"
  ) {
    // Given a single abandoned directory containing multiple instances and features
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

    // When Initialize is called
    SdkStorage storage(fs, 12345);
    REQUIRE(storage.Initialize(logger, "my-app/storage", "main"));

    // Then the directory is renamed atomically to our PID, preserving the full
    // directory tree across all instances and features
    REQUIRE(!DirectoryExists(fs, "my-app/storage/.datadog/99999"));
    REQUIRE(DirectoryExists(fs, "my-app/storage/.datadog/12345"));

    // And all events are accessible under the renamed directory
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

  SECTION("M skip abandoned directory W no lockfile exists") {
    // Given a directory for a numeric PID but with no accompanying lockfile
    CreateAbandonedDirectory(fs, "my-app/storage", "99999", "main", "rum", false);
    WriteTestEvent(
        fs, "my-app/storage", "99999", "main", "rum", "v1", "event.json", "{\"id\": 1}"
    );

    // When Initialize is called
    SdkStorage storage(fs, 12345);
    REQUIRE(storage.Initialize(logger, "my-app/storage", "main"));

    // Then the directory is not claimed (without a lockfile we cannot verify the
    // process is dead, so we leave it alone)
    REQUIRE(DirectoryExists(fs, "my-app/storage/.datadog/99999"));
    REQUIRE(VerifyEventFile(
        fs, "my-app/storage", "99999", "main", "rum", "v1", "event.json", "{\"id\": 1}"
    ));

    // And our new directory is created normally
    REQUIRE(DirectoryExists(fs, "my-app/storage/.datadog/12345"));
  }

  SECTION("M rename empty abandoned directory W process is dead") {
    // Given an empty abandoned directory for a dead process
    CreateAbandonedDirectory(fs, "my-app/storage", "99999", "main", "rum", true);

    // When Initialize is called
    SdkStorage storage(fs, 12345);
    REQUIRE(storage.Initialize(logger, "my-app/storage", "main"));

    // Then the empty directory is renamed atomically to our PID
    REQUIRE(!DirectoryExists(fs, "my-app/storage/.datadog/99999"));
    REQUIRE(DirectoryExists(fs, "my-app/storage/.datadog/12345"));

    // And our lockfile is created
    REQUIRE(FileExists(fs, "my-app/storage/.datadog/12345.lock"));
  }

  SECTION("M not orphan abandoned directory W lockfile acquisition fails") {
    // Given an abandoned directory with events, and our own lockfile slot already
    // held by another process (simulating a race or a stale lock)
    fs.Mkdirs("my-app/storage/.datadog");
    fs.Touch("my-app/storage/.datadog/12345.lock");

    PlatformPath held_path;
    REQUIRE(held_path.Encode("my-app/storage/.datadog/12345.lock"));
    auto held = fs.OpenForWrite(held_path, false, true);
    REQUIRE(held.value == FilesystemResult::OK);
    REQUIRE(held.handle != INVALID_FILE_HANDLE);

    CreateAbandonedDirectory(fs, "my-app/storage", "99999", "main", "rum", true);
    WriteTestEvent(
        fs, "my-app/storage", "99999", "main", "rum", "v1", "event.json", "{\"id\": 1}"
    );

    // When Initialize is called
    SdkStorage storage(fs, 12345);

    // Then Initialize fails because the lockfile cannot be acquired
    REQUIRE(!storage.Initialize(logger, "my-app/storage", "main"));

    // And the abandoned directory is untouched — it retains its own lockfile and can
    // be recovered by a future process. With the old scan-before-lockfile ordering,
    // TryClaimAbandonedDirectory would have renamed 99999/ → 12345/ and deleted
    // 99999.lock before the lockfile acquisition failed, permanently orphaning the
    // data. With lockfile-before-scan ordering, Initialize() aborts before scanning.
    REQUIRE(DirectoryExists(fs, "my-app/storage/.datadog/99999"));
    REQUIRE(FileExists(fs, "my-app/storage/.datadog/99999.lock"));
    REQUIRE(!DirectoryExists(fs, "my-app/storage/.datadog/12345"));

    fs.Close(held.handle);
  }
}
