// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/storage/sdk.hpp"

#include <cstdint>
#include <cstring>

#include "mock/filesystem_new.hpp"
#include "support/catch.hpp"
#include "support/diagnostics.hpp"
#include "support/filesystem.hpp"

using namespace datadog::impl;

TEST_CASE("SdkStorage directory creation", "[unit][storage]") {
  // Given a mock filesystem with an existing application-specific storage directory
  MockFilesystemNew fs;
  fs.Mkdirs("app");
  REQUIRE(!fs.IsDirectory("app/.datadog"));

  // And a logger that will buffer all diagnostic messages emitted
  DiagnosticMessageBuffer diagnostics;
  DiagnosticLogger logger = diagnostics.CreateTestLogger();

  SECTION("M create current-PID subdirectory W Initialize is called") {
    // Given a storage object for SDK instance 'main' in PID 12345
    SdkStorage storage(fs, logger, 12345);

    // When we initialize storage with a valid, preexisting storage directory path
    const bool ok = storage.Initialize("app", "main");

    // Then initialization succeeds
    REQUIRE(ok);

    // And the initialization process prepares the requisite instance-level
    // and process-level subdirectories beneath <application-storage/.datadog/
    REQUIRE(fs.IsDirectory("app/.datadog"));
    REQUIRE(fs.IsDirectory("app/.datadog/main"));
    REQUIRE(fs.IsDirectory("app/.datadog/main/12345"));

    // And no errors or warnings are reported
    REQUIRE(diagnostics.error.size() == 0);
    REQUIRE(diagnostics.warning.size() == 0);
  }

  SECTION("M fail initialization W PID is not positive") {
    // Given a storage object constructed with an invalid (non-positive) PID value
    const int64_t bad_pid = GENERATE(as<int64_t>{}, 0, -1, -999);
    SdkStorage storage(fs, logger, bad_pid);

    // When we attempt to initialize storage
    const bool ok = storage.Initialize("app", "main");

    // Then initialization fails
    REQUIRE(!ok);

    // And an error is reported
    REQUIRE(diagnostics.error.size() > 0);
  }

  SECTION("M create artifact storage directory W InitializeArtifactStorage is called") {
    // Given properly-initialized SDK storage
    SdkStorage storage(fs, logger, 12345);
    REQUIRE(storage.Initialize("app", "main"));

    // When we call InitializeArtifactStorage to prepare an arbitrary, feature-specific
    // subdirectory for artifacts that we want to persist to disk
    auto artifacts = storage.InitializeArtifactStorage(".things");

    // Then the call succeeds and returns a valid ArtifactStorage value
    REQUIRE(artifacts != nullptr);

    // And we now have a subdirectory in which to store things
    REQUIRE(fs.IsDirectory("app/.datadog/.things"));

    // And no errors or warnings are reported
    REQUIRE(diagnostics.error.size() == 0);
    REQUIRE(diagnostics.warning.size() == 0);
  }

  SECTION(
      "M create feature event storage directories W InitializeFeatureEventStorage is "
      "called"
  ) {
    // Given properly-initialized SDK storage
    SdkStorage storage(fs, logger, 12345);
    REQUIRE(storage.Initialize("app", "main"));

    // When we call InitializeFeatureEventStorage to prepare a set of directories to
    // store event data on behalf of a specific feature
    auto events = storage.InitializeFeatureEventStorage("foo");

    // Then the call succeeds and returns a valid FeatureEventStorage value
    REQUIRE(events != nullptr);

    // And we now have a feature-level storage directory, with subdirectories for both
    // consent-pending and consent-granted events
    REQUIRE(fs.IsDirectory("app/.datadog/main/12345/foo"));
    REQUIRE(fs.IsDirectory("app/.datadog/main/12345/foo/intermediate-v1"));
    REQUIRE(fs.IsDirectory("app/.datadog/main/12345/foo/v1"));

    // And no errors or warnings are reported
    REQUIRE(diagnostics.error.size() == 0);
    REQUIRE(diagnostics.warning.size() == 0);
  }

  SECTION("M initialize successfully W directories already exist") {
    // Given a filesystem where the directories that the SDK wants to create already
    // exist
    fs.Mkdirs("app/.datadog/.things");
    fs.Mkdirs("app/.datadog/main/12345/foo/intermediate-v1");
    fs.Mkdirs("app/.datadog/main/12345/foo/v1");

    // And some existing files in those directories
    fs.Touch("app/.datadog/.things/blob", "thing blob");
    fs.Touch("app/.datadog/main/12345/foo/v1/blob", "foo blob");

    // When we initialize an SDK instance
    SdkStorage storage(fs, logger, 12345);
    const bool ok = storage.Initialize("app", "main");

    // Then it picks up on those existing directories and initializes without error
    REQUIRE(ok);

    // Next: when we initialize artifact and event storage with existing directories
    auto artifacts = storage.InitializeArtifactStorage(".things");
    auto events = storage.InitializeFeatureEventStorage("foo");

    // Then they work fine too
    REQUIRE(artifacts != nullptr);
    REQUIRE(events != nullptr);

    // And all our directories still exist
    REQUIRE(fs.IsDirectory("app/.datadog/.things"));
    REQUIRE(fs.IsDirectory("app/.datadog/main/12345/foo/intermediate-v1"));
    REQUIRE(fs.IsDirectory("app/.datadog/main/12345/foo/v1"));

    // And existing files remain untouched
    REQUIRE(fs.Cat("app/.datadog/.things/blob") == "thing blob");
    REQUIRE(fs.Cat("app/.datadog/main/12345/foo/v1/blob") == "foo blob");

    // And no errors or warnings are reported
    REQUIRE(diagnostics.error.size() == 0);
    REQUIRE(diagnostics.warning.size() == 0);
  }

  SECTION(
      "M fail to initialize W configured application storage directory does not exist"
  ) {
    // When we initialize an SDK instance with nonexistent-dir/ as its storage directory
    SdkStorage storage(fs, logger, 12345);
    const bool ok = storage.Initialize("nonexistent-dir", "main");

    // Then initialization fails, because the SDK will create
    // <application-storage>/.datadog/ et al., but it will not attempt to create
    // <application-storage>/ itself
    REQUIRE(!ok);

    // And we get a diagnostic error reporting why initialization failed
    REQUIRE(diagnostics.warning.size() == 0);
    REQUIRE(diagnostics.error.size() == 1);
    RequireFilesystemError(
        diagnostics.error[0],
        "DoesNotExist",
        "nonexistent-dir/.datadog",
        "unable to create directory"
    );
  }

  SECTION("{error handling: excessive path length}") {
    // MAX_STORAGE_PATH_SIZE places a hardcoded upper bound on the length of filesystem
    // paths that we can work with: if we hit this limit because the application has
    // provided an excessively-long root path for the application storage directory, the
    // SDK will handle this gracefully by logging an error and aborting storage init
    struct TestParams {
      size_t application_storage_path_len;
      std::string artifact_dir_name;
      std::string feature_name;
      bool want_error;
      std::string want_relpath;
    };
    std::vector<TestParams> tests = {
        // n.b. the API should reject root storage path values at well below the
        // 511-char threshold represented by MAX_STORAGE_PATH_SIZE: these tests just
        // ensure proper coverage for error handling paths in SdkStorage et al.
        {260, "", "", false, ""},
        {486, "", "", false, ""},
        {487, "", "", true, ".datadog/main/12345.lock"},
        {492, "", "", true, ".datadog/main/12345"},
        {498, "", "", true, ".datadog/main"},
        {503, "", "", true, ".datadog"},
        // InitializeArtifactStorage
        {486, ".artifacts", "", false, ""},
        {486, ".longish-directory-name", "", true, ".datadog/.longish-directory-name"},
        // InitializeFeatureEventStorage
        {468, "", "foobar", false, ""},
        {468, "", "foobarbaz", true, ".datadog/main/12345/foobarbaz/intermediate-v1"},
        {482, "", "foobarbaz", true, ".datadog/main/12345/foobarbaz"}
    };
    for (const auto& tt : tests) {
      REQUIRE(tt.want_relpath.empty() == !tt.want_error);
      DYNAMIC_SECTION(
          "M " << (tt.want_error ? ("fail at " + tt.want_relpath) : "succeed")
               << " W application storage path has len="
               << tt.application_storage_path_len
               << (tt.artifact_dir_name.empty()
                       ? ""
                       : (", artifact=" + tt.artifact_dir_name))
               << (tt.feature_name.empty() ? "" : (", feature=" + tt.feature_name))
      ) {
        // Given a configured application storage path with the desired length
        std::string application_storage_path(tt.application_storage_path_len, 'a');
        fs.Mkdirs(application_storage_path);

        // When we attempt to initialize SdkStorage with that path, and with an SDK
        // instance name of 'main' (len: 4) and a PID of 12345 (len: 5)
        SdkStorage storage(fs, logger, 12345);
        bool ok = storage.Initialize(application_storage_path, "main");

        // And, if SDK initialization didn't fail, we proceed to initialize artifact
        // storage with the given name if desired
        if (ok && !tt.artifact_dir_name.empty()) {
          auto artifacts = storage.InitializeArtifactStorage(tt.artifact_dir_name);
          ok = artifacts != nullptr;
        }

        // And, if we still haven't failed, we initialize event storage for a feature
        // with the given name if desired
        if (ok && !tt.feature_name.empty()) {
          auto events = storage.InitializeFeatureEventStorage(tt.feature_name);
          ok = events != nullptr;
        }

        // Then we get the result asserted by this test case
        if (!tt.want_error) {
          // If we expect no error, then initialization succeeds without error
          REQUIRE(ok);
          REQUIRE(diagnostics.error.size() == 0);
        } else {
          // Otherwise, initialization fails and we get a single error indicating that
          // our configured application storage path, or a child path built from it, is
          // over the length limit
          REQUIRE(!ok);
          REQUIRE(diagnostics.error.size() == 1);
          const std::string& err = diagnostics.error[0];
          REQUIRE(err.find("path exceeds length limit") != std::string::npos);

          // And the error indicates the path that was over the limit, and it's the
          // subdirectory that we expected to trip the limit
          auto want_err_substr = [&application_storage_path](std::string_view s) {
            // Split on the last '/' to get the parent directory and leaf name
            std::string parent_path;
            std::string name;
            const size_t last_slash = s.rfind('/');
            if (last_slash != std::string_view::npos) {
              parent_path =
                  application_storage_path + "/" + std::string(s.substr(0, last_slash));
              name = std::string(s.substr(last_slash + 1));
            } else {
              parent_path = application_storage_path;
              name = std::string(s);
            }

            // If the leaf has an extension (e.g. "12345.lock"), the error will be
            // formatted differently from a plain directory name
            const size_t dot_pos = name.find_last_of('.');
            if (dot_pos != std::string::npos && dot_pos > 0 &&
                dot_pos < name.size() - 1) {
              const std::string path_noext =
                  parent_path + "/" + name.substr(0, dot_pos);
              const std::string ext = name.substr(dot_pos);
              return "\"path\":" + GetJsonLiteralForPath(path_noext) +
                     ",\"ext\":" + GetJsonLiteralForPath(ext);
            }
            return "\"parent_path\":" + GetJsonLiteralForPath(parent_path) +
                   ",\"name\":" + GetJsonLiteralForPath(name);
          };
          REQUIRE(err.find(want_err_substr(tt.want_relpath)) != std::string::npos);
        }
      }
    }
  }

  SECTION("{error handling: directory creation failure}") {
    // If the filesystem refuses to create a directory that's required for SDK storage
    // to work properly, we will handle that failure gracefully
    struct TestParams {
      // Attempts to list files, or create subdirectories, or read/write files within
      // this directory will fail with a simulated PermissionDenied error
      std::string inaccessible_dir_path;

      // We'll attempt to create an artifact and/or feature storage directory with the
      // given name, if initial storage init is successful
      std::string artifact_dir_name;
      std::string feature_name;

      // We expect initialization to fail when attempting a filesystem call on this path
      std::string want_relpath;
    };
    std::vector<TestParams> tests = {
        {"app", "", "", "app/.datadog"},
        {"app/.datadog", "", "", "app/.datadog/main"},
        {"app/.datadog/main", "", "", "app/.datadog/main/12345.lock"},
        {"app/.datadog/main/12345", "", "foobar", "app/.datadog/main/12345/foobar"},
        {"app/.datadog/main/12345/foobar",
         "",
         "foobar",
         "app/.datadog/main/12345/foobar/intermediate-v1"}
    };
    for (const auto& tt : tests) {
      DYNAMIC_SECTION(
          "M fail on "
          << tt.want_relpath << " W " << tt.inaccessible_dir_path << "is inaccessible"
          << (tt.artifact_dir_name.empty() ? ""
                                           : (", artifact=" + tt.artifact_dir_name))
          << (tt.feature_name.empty() ? "" : (", feature=" + tt.feature_name))
      ) {
        // Given a fileystem that does not permit the SDK to access the given path
        fs.Mkdirs(tt.inaccessible_dir_path);
        fs.SimulateFailure(
            tt.inaccessible_dir_path, FilesystemResult::PermissionDenied
        );

        // When we attempt to initialize SDK storage
        SdkStorage storage(fs, logger, 12345);
        bool ok = storage.Initialize("app", "main");

        // And optionally initialize artifact storage
        if (ok && !tt.artifact_dir_name.empty()) {
          auto artifacts = storage.InitializeArtifactStorage(tt.artifact_dir_name);
          ok = artifacts != nullptr;
        }

        // And optionally initialize feature event storage
        if (ok && !tt.feature_name.empty()) {
          auto events = storage.InitializeFeatureEventStorage(tt.feature_name);
          ok = events != nullptr;
        }

        // Then storage initialization fails
        REQUIRE(!ok);

        // And we get a single error indicating that we failed a required filesystem
        // operation targeting the expected path
        std::string path = tt.want_relpath;
        REQUIRE(diagnostics.error.size() == 1);
        RequireFilesystemError(
            diagnostics.error[0], "PermissionDenied", path, "unable"
        );
      }
    }
  }
}

TEST_CASE("SdkStorage process lockfile", "[unit][storage]") {
  // Given a mock filesystem with an existing application-specific storage directory
  MockFilesystemNew fs;
  fs.Mkdirs("app");
  REQUIRE(!fs.IsDirectory("app/.datadog"));

  // And a logger that will buffer all diagnostic messages emitted
  DiagnosticMessageBuffer diagnostics;
  DiagnosticLogger logger = diagnostics.CreateTestLogger();

  SECTION("M create <pid>.lock and acquire lock W Initialize is called") {
    // When we initialize a storage object for SDK instance 'main' in PID 12345
    SdkStorage storage(fs, logger, 12345);
    REQUIRE(storage.Initialize("app", "main"));

    // Then main/12345.lock is created alongside the main/12345/ directory
    REQUIRE(fs.IsDirectory("app/.datadog/main/12345"));
    REQUIRE(fs.IsFile("app/.datadog/main/12345.lock"));

    // And 12345.lock is locked
    REQUIRE(fs.IsFileLocked("app/.datadog/main/12345.lock"));

    // And no errors or warnings are reported
    REQUIRE(diagnostics.error.size() == 0);
    REQUIRE(diagnostics.warning.size() == 0);
  }

  SECTION("M release <pid>.lock W leaving scope after successful initialization") {
    // Given that 12345.lock does not yet exist
    REQUIRE(!fs.IsFile("app/.datadog/main/12345.lock"));

    // When we initialize a storage object and acquire 12345.lock
    {
      SdkStorage storage(fs, logger, 12345);
      REQUIRE(storage.Initialize("app", "main"));
      REQUIRE(fs.IsFileLocked("app/.datadog/main/12345.lock"));

      // And allow SdkStorage to go out of scope and be destroyed
    }

    // Then the lock file remains on disk
    REQUIRE(fs.IsFile("app/.datadog/main/12345.lock"));

    // And we no longer hold a lock on the file
    REQUIRE(!fs.IsFileLocked("app/.datadog/main/12345.lock"));

    // And no errors or warnings are reported
    REQUIRE(diagnostics.error.size() == 0);
    REQUIRE(diagnostics.warning.size() == 0);
  }

  SECTION("M Initialize OK W <pid>.lock already exists but is available") {
    // Given an existing file main/12345.lock which is NOT currently locked
    fs.Touch("app/.datadog/main/12345.lock");
    REQUIRE(fs.IsFile("app/.datadog/main/12345.lock"));
    REQUIRE(!fs.IsFileLocked("app/.datadog/main/12345.lock"));

    // When we initialize a storage object for SDK instance 'main' in PID 12345
    SdkStorage storage(fs, logger, 12345);
    const bool ok = storage.Initialize("app", "main");

    // Then initialization is successful
    REQUIRE(ok);

    // And the lock file still exists at the same path
    REQUIRE(fs.IsFile("app/.datadog/main/12345.lock"));

    // And it's now been claimed
    REQUIRE(fs.IsFileLocked("app/.datadog/main/12345.lock"));

    // And no errors or warnings are reported
    REQUIRE(diagnostics.error.size() == 0);
    REQUIRE(diagnostics.warning.size() == 0);
  }

  SECTION("M fail to Initialize W <pid>.lock already exists and is already locked") {
    // Given an existing file main/12345.lock which is already locked
    fs.Touch("app/.datadog/main/12345.lock");
    fs.LockFile("app/.datadog/main/12345.lock");
    REQUIRE(fs.IsFile("app/.datadog/main/12345.lock"));
    REQUIRE(fs.IsFileLocked("app/.datadog/main/12345.lock"));

    // When we initialize a storage object for SDK instance 'main' in PID 12345
    SdkStorage storage(fs, logger, 12345);
    const bool ok = storage.Initialize("app", "main");

    // Then initialization fails
    REQUIRE(!ok);

    // And the lock file still exists at the same path and is still locked
    REQUIRE(fs.IsFile("app/.datadog/main/12345.lock"));
    REQUIRE(fs.IsFileLocked("app/.datadog/main/12345.lock"));

    // And we get an error explaining that initialization failed because another process
    // already owns 12345.lock
    REQUIRE(diagnostics.warning.size() == 0);
    REQUIRE(diagnostics.error.size() == 1);
    RequireFilesystemError(
        diagnostics.error[0],
        "LockContention",
        "app/.datadog/main/12345.lock",
        "unable to acquire lockfile"
    );
  }
}

TEST_CASE("SdkStorage event migration", "[unit][storage]") {
  // Given a mock filesystem and diagnostic logger
  MockFilesystemNew fs;
  fs.Mkdirs("app");
  DiagnosticMessageBuffer diagnostics;
  DiagnosticLogger logger = diagnostics.CreateTestLogger();

  // And event data written by an application with PID 444, in which an SDK with
  // instance name 'main' wrote multiple batches of event data for features 'foo' and
  // 'bar' to both pending-consent and consent-granted directories
  fs.Mkdirs("app/.datadog/main/444/foo/intermediate-v1");
  fs.Mkdirs("app/.datadog/main/444/foo/v1");
  fs.Mkdirs("app/.datadog/main/444/bar/intermediate-v1");
  fs.Mkdirs("app/.datadog/main/444/bar/v1");
  fs.Touch("app/.datadog/main/444/foo/intermediate-v1/1700000000000", "444-foo-1");
  fs.Touch("app/.datadog/main/444/bar/v1/1700000000500", "444-bar-1");
  fs.Touch("app/.datadog/main/444/bar/v1/1700000000600", "444-bar-2");

  SECTION("M migrate on init W storage contains a single abandoned process's data") {
    // Regardless of whether main/444.lock exists (so long as it isn't locked by another
    // process)
    const bool with_existing_pid_file = GENERATE(false, true);
    if (with_existing_pid_file) {
      fs.Touch("app/.datadog/main/444.lock");
    }

    // When we initialize storage for a new SDK instance (with the same instance name,
    // 'main') and PID 555
    SdkStorage storage(fs, logger, 555);
    const bool ok = storage.Initialize("app", "main");

    // Then initialization completes OK
    REQUIRE(ok);

    // And the original process directory is now gone, along with the lock file (if one
    // was present)
    REQUIRE(!fs.IsFile("app/.datadog/main/444.lock"));
    REQUIRE(!fs.IsDirectory("app/.datadog/main/444"));

    // And a new process directory is created, with all events from PID 444 seamlessly
    // migrated into the corresponding directory for PID 555
    REQUIRE(fs.IsFile("app/.datadog/main/555.lock"));
    REQUIRE(fs.IsFileLocked("app/.datadog/main/555.lock"));
    REQUIRE(fs.IsDirectory("app/.datadog/main/555"));
    REQUIRE(fs.IsFile("app/.datadog/main/555/foo/intermediate-v1/1700000000000"));
    REQUIRE(fs.Cat("app/.datadog/main/555/bar/v1/1700000000500") == "444-bar-1");
    REQUIRE(fs.Cat("app/.datadog/main/555/bar/v1/1700000000600") == "444-bar-2");

    // And no errors or warnings are reported
    REQUIRE(diagnostics.error.size() == 0);
    REQUIRE(diagnostics.warning.size() == 0);

    // A a single line of status output indicates that the migration was accomplished
    // via rename
    REQUIRE(diagnostics.status.size() == 1);
    REQUIRE(
        diagnostics.status[0].find(
            "Migrated data from abandoned process via directory rename"
        ) != std::string::npos
    );
    REQUIRE(diagnostics.status[0].find("444") != std::string::npos);
  }

  SECTION("M not migrate on init W other process still holds lock") {
    // Given an existing main/444.lock file which is still locked by another process
    fs.Touch("app/.datadog/main/444.lock");
    fs.LockFile("app/.datadog/main/444.lock");

    // When we initialize storage for 'main' with PID 555
    SdkStorage storage(fs, logger, 555);
    const bool ok = storage.Initialize("app", "main");

    // Then initialization completes OK
    REQUIRE(ok);

    // And the appropriate files and directories are created for our new instance
    REQUIRE(fs.IsFile("app/.datadog/main/555.lock"));
    REQUIRE(fs.IsFileLocked("app/.datadog/main/555.lock"));
    REQUIRE(fs.IsDirectory("app/.datadog/main/555"));

    // But PID 444's data is not migrated: since we were unable to acquire
    // main/444.lock, we left the data in main/444/ entirely untouched
    REQUIRE(fs.IsFile("app/.datadog/main/444.lock"));
    REQUIRE(fs.IsFileLocked("app/.datadog/main/444.lock"));
    REQUIRE(fs.IsDirectory("app/.datadog/main/444"));
    REQUIRE(fs.IsFile("app/.datadog/main/444/foo/intermediate-v1/1700000000000"));
    REQUIRE(!fs.IsFile("app/.datadog/main/555/foo/intermediate-v1/1700000000000"));
    REQUIRE(fs.Cat("app/.datadog/main/444/bar/v1/1700000000500") == "444-bar-1");
    REQUIRE(fs.Cat("app/.datadog/main/444/bar/v1/1700000000600") == "444-bar-2");

    // And no errors or warnings are reported
    REQUIRE(diagnostics.error.size() == 0);
    REQUIRE(diagnostics.warning.size() == 0);
  }

  SECTION("{multiple existing processes}") {
    // Given another directory for PID 333, which also contains foo and bar data from a
    // 'main' SDK instance
    fs.Mkdirs("app/.datadog/main/333/foo/intermediate-v1");
    fs.Mkdirs("app/.datadog/main/333/foo/v1");
    fs.Mkdirs("app/.datadog/main/333/bar/intermediate-v1");
    fs.Mkdirs("app/.datadog/main/333/bar/v1");
    fs.Touch("app/.datadog/main/333/foo/v1/1700000000030", "333-foo-1");
    fs.Touch("app/.datadog/main/333/bar/v1/1700000000530", "333-bar-1");
    fs.Touch("app/.datadog/main/333.lock");

    SECTION("M merge event data W multiple abandoned processes have data on disk") {
      // Given that neither main/333.lock nor main/444.lock are claimed

      // When we initialize storage for 'main' with PID 555
      SdkStorage storage(fs, logger, 555);
      const bool ok = storage.Initialize("app", "main");

      // Then initialization completes OK
      REQUIRE(ok);

      // And both original process directories and lockfiles are now gone
      REQUIRE(!fs.IsFile("app/.datadog/main/333.lock"));
      REQUIRE(!fs.IsDirectory("app/.datadog/main/333"));
      REQUIRE(!fs.IsFile("app/.datadog/main/444.lock"));
      REQUIRE(!fs.IsDirectory("app/.datadog/main/444"));

      // And a new process directory is created, with all events from both PID 333 and
      // PID 444 seamlessly migrated into the corresponding directory for PID 555
      REQUIRE(fs.IsFile("app/.datadog/main/555.lock"));
      REQUIRE(fs.IsFileLocked("app/.datadog/main/555.lock"));
      REQUIRE(fs.IsDirectory("app/.datadog/main/555"));
      REQUIRE(fs.IsFile("app/.datadog/main/555/foo/intermediate-v1/1700000000000"));
      REQUIRE(fs.IsFile("app/.datadog/main/555/foo/v1/1700000000030"));
      REQUIRE(fs.Cat("app/.datadog/main/555/bar/v1/1700000000500") == "444-bar-1");
      REQUIRE(fs.Cat("app/.datadog/main/555/bar/v1/1700000000530") == "333-bar-1");
      REQUIRE(fs.Cat("app/.datadog/main/555/bar/v1/1700000000600") == "444-bar-2");

      // And no errors or warnings are reported
      REQUIRE(diagnostics.error.size() == 0);
      REQUIRE(diagnostics.warning.size() == 0);

      // And status output indicates which directories were migrated, and by which means
      REQUIRE(diagnostics.status.size() == 2);
      REQUIRE(
          diagnostics.status[0].find(
              "Migrated data from abandoned process via directory rename"
          ) != std::string::npos
      );
      REQUIRE(diagnostics.status[0].find("333") != std::string::npos);

      REQUIRE(
          diagnostics.status[1].find("Claimed all event data from abandoned process") !=
          std::string::npos
      );
      REQUIRE(diagnostics.status[1].find("444") != std::string::npos);
    }

    SECTION("M claim 444 by rename W 333 is locked") {
      // Given that main/333.lock is held by another process, leaving only 444 abandoned
      fs.LockFile("app/.datadog/main/333.lock");

      // When we successfully initialize storage for 'main' with PID 555
      SdkStorage storage(fs, logger, 555);
      REQUIRE(storage.Initialize("app", "main"));
      REQUIRE(fs.IsFile("app/.datadog/main/555.lock"));
      REQUIRE(fs.IsFileLocked("app/.datadog/main/555.lock"));
      REQUIRE(fs.IsDirectory("app/.datadog/main/555"));

      // Then the data for PID 444 is now present in our new storage directory
      REQUIRE(!fs.IsFile("app/.datadog/main/444.lock"));
      REQUIRE(!fs.IsDirectory("app/.datadog/main/444"));
      REQUIRE(fs.IsFile("app/.datadog/main/555/foo/intermediate-v1/1700000000000"));
      REQUIRE(fs.Cat("app/.datadog/main/555/bar/v1/1700000000500") == "444-bar-1");
      REQUIRE(fs.Cat("app/.datadog/main/555/bar/v1/1700000000600") == "444-bar-2");

      // And the data for PID 333 remains untouched
      REQUIRE(fs.IsFile("app/.datadog/main/333.lock"));
      REQUIRE(fs.IsFileLocked("app/.datadog/main/333.lock"));
      REQUIRE(fs.IsDirectory("app/.datadog/main/333"));
      REQUIRE(fs.Cat("app/.datadog/main/333/bar/v1/1700000000530") == "333-bar-1");

      // And no errors or warnings are reported
      REQUIRE(diagnostics.error.size() == 0);
      REQUIRE(diagnostics.warning.size() == 0);

      // And status output shows 444 was claimed via rename
      REQUIRE(diagnostics.status.size() == 1);
      REQUIRE(
          diagnostics.status[0].find(
              "Migrated data from abandoned process via directory rename"
          ) != std::string::npos
      );
      REQUIRE(diagnostics.status[0].find("444") != std::string::npos);
    }

    SECTION("M migrate no data W 333 and 444 are both locked") {
      // Given that main/333.lock and main/444.lock are both held by other processes
      fs.LockFile("app/.datadog/main/333.lock");
      fs.Touch("app/.datadog/main/444.lock");
      fs.LockFile("app/.datadog/main/444.lock");

      // When we successfully initialize storage for 'main' with PID 555
      SdkStorage storage(fs, logger, 555);
      REQUIRE(storage.Initialize("app", "main"));
      REQUIRE(fs.IsFile("app/.datadog/main/555.lock"));
      REQUIRE(fs.IsFileLocked("app/.datadog/main/555.lock"));
      REQUIRE(fs.IsDirectory("app/.datadog/main/555"));

      // Then our new storage directory contains no migrated data
      REQUIRE(!fs.IsDirectory("app/.datadog/main/555/foo"));
      REQUIRE(!fs.IsDirectory("app/.datadog/main/555/bar"));

      // And both existing processes' data remains untouched
      REQUIRE(fs.IsFile("app/.datadog/main/333.lock"));
      REQUIRE(fs.IsFileLocked("app/.datadog/main/333.lock"));
      REQUIRE(fs.IsDirectory("app/.datadog/main/333"));
      REQUIRE(fs.Cat("app/.datadog/main/333/bar/v1/1700000000530") == "333-bar-1");
      REQUIRE(fs.IsFile("app/.datadog/main/444.lock"));
      REQUIRE(fs.IsFileLocked("app/.datadog/main/444.lock"));
      REQUIRE(fs.IsDirectory("app/.datadog/main/444"));
      REQUIRE(fs.Cat("app/.datadog/main/444/bar/v1/1700000000500") == "444-bar-1");

      // And no errors or warnings are reported
      REQUIRE(diagnostics.error.size() == 0);
      REQUIRE(diagnostics.warning.size() == 0);
    }

    SECTION("M complete file-by-file merge W loose files exist in abandoned dir") {
      // Given a bunch of stray files that have been written into the storage directory
      // for abandoned process 444
      fs.Touch("app/.datadog/main/444/.DS_Store");
      fs.Touch("app/.datadog/main/444/foo/.DS_Store");
      fs.Touch("app/.datadog/main/444/bar/intermediate-v1/.DS_Store");
      fs.Touch("app/.datadog/main/444/bar/v1/.DS_Store");

      // When we initialize storage for 'main' with PID 555 (with neither main/333.lock
      // nor main/444.lock claimed, resulting in a rename of main/333/ and then an
      // attempt to move all files from main/444/ into the new directory)
      SdkStorage storage(fs, logger, 555);
      const bool ok = storage.Initialize("app", "main");

      // Then initialization completes OK
      REQUIRE(ok);

      // And both original process directories and lockfiles are now gone
      REQUIRE(!fs.IsFile("app/.datadog/main/333.lock"));
      REQUIRE(!fs.IsDirectory("app/.datadog/main/333"));
      REQUIRE(!fs.IsFile("app/.datadog/main/444.lock"));
      REQUIRE(!fs.IsDirectory("app/.datadog/main/444"));

      // And a new process directory is created, with all events from both PID 333 and
      // PID 444 seamlessly migrated into the corresponding directory for PID 555
      REQUIRE(fs.IsFile("app/.datadog/main/555.lock"));
      REQUIRE(fs.IsFileLocked("app/.datadog/main/555.lock"));
      REQUIRE(fs.IsDirectory("app/.datadog/main/555"));
      REQUIRE(fs.IsFile("app/.datadog/main/555/foo/intermediate-v1/1700000000000"));
      REQUIRE(fs.IsFile("app/.datadog/main/555/foo/v1/1700000000030"));
      REQUIRE(fs.Cat("app/.datadog/main/555/bar/v1/1700000000500") == "444-bar-1");
      REQUIRE(fs.Cat("app/.datadog/main/555/bar/v1/1700000000530") == "333-bar-1");
      REQUIRE(fs.Cat("app/.datadog/main/555/bar/v1/1700000000600") == "444-bar-2");

      // And no errors or warnings are reported
      REQUIRE(diagnostics.error.size() == 0);
      REQUIRE(diagnostics.warning.size() == 0);

      // And we get the usual status messages for successful migration, but also a
      // couple of messages indicating that both 444/.DS_Store and 444/foo/.DS_Store
      // were deleted
      REQUIRE(diagnostics.status.size() == 4);
      REQUIRE(diagnostics.status[0].find("Migrated data") == 0);
      REQUIRE(
          diagnostics.status[1].find(
              "Deleted loose file on storage directory migration"
          ) != std::string::npos
      );
      REQUIRE(diagnostics.status[1].find(".DS_Store") != std::string::npos);
      REQUIRE(diagnostics.status[2].find("Claimed all event data") == 0);
      REQUIRE(
          diagnostics.status[3].find(
              "Deleted loose file on storage directory migration"
          ) != std::string::npos
      );
      REQUIRE(diagnostics.status[3].find(".DS_Store") != std::string::npos);

      // And the leaf .DS_Store files within intermediate-v1/ and v1/ have just been
      // silently migrated, since the file-by-file rename does not differentiate between
      // event files and other files; it just yeets everything over and lets the active
      // SDK instance deal with it
      REQUIRE(fs.IsFile("app/.datadog/main/555/bar/intermediate-v1/.DS_Store"));
      REQUIRE(fs.IsFile("app/.datadog/main/555/bar/v1/.DS_Store"));
    }

    SECTION(
        "M delete file from src_dir and complete file-by-file merge W batch file with "
        "same name exists in dst_dir"
    ) {
      // Given a batch file that was written to PID 333's storage with the exact same
      // timestamp as an existing batch in PID 444
      fs.Touch("app/.datadog/main/333/bar/v1/1700000000600", "333-bar-2");
      REQUIRE(fs.Cat("app/.datadog/main/444/bar/v1/1700000000600") == "444-bar-2");

      // When we initialize storage for 'main' with PID 555, with main/333/ to be
      // renamed and main/444/ to be merged in file-by-file
      SdkStorage storage(fs, logger, 555);
      const bool ok = storage.Initialize("app", "main");

      // Then initialization completes OK
      REQUIRE(ok);

      // And both original process directories and lockfiles are now gone
      REQUIRE(!fs.IsFile("app/.datadog/main/333.lock"));
      REQUIRE(!fs.IsDirectory("app/.datadog/main/333"));
      REQUIRE(!fs.IsFile("app/.datadog/main/444.lock"));
      REQUIRE(!fs.IsDirectory("app/.datadog/main/444"));

      // And a new process directory is created, with all events from both PID 333 and
      // PID 444 seamlessly migrated into the corresponding directory for PID 555
      REQUIRE(fs.IsFile("app/.datadog/main/555.lock"));
      REQUIRE(fs.IsFileLocked("app/.datadog/main/555.lock"));
      REQUIRE(fs.IsDirectory("app/.datadog/main/555"));
      REQUIRE(fs.IsFile("app/.datadog/main/555/foo/intermediate-v1/1700000000000"));
      REQUIRE(fs.IsFile("app/.datadog/main/555/foo/v1/1700000000030"));

      // And the version of the conflicting file (bar/v1/1700000000600) that remains in
      // the new storage directory is the version from 333, since:
      // - main/333/ was renamed to main/555/
      // - main/444/ was then merged into main/555/
      // - on conflict, src_file was from 444/ and dst_file was in 555/ (w/ "333-bar-2")
      // - conflict behavior: src_file is dropped; dst_file is preserved
      // (Whether 333/ or 444/ prevails is not important; but it'll always play out this
      // way in tests since the mock filesystem lists subdirectories with deterministic,
      // lexically-sorted ordering)
      REQUIRE(fs.Cat("app/.datadog/main/555/bar/v1/1700000000600") == "333-bar-2");

      // And no errors or are reported
      REQUIRE(diagnostics.error.size() == 0);

      // And we get a single warning indicating that the 444's version of
      // bar/v1/1700000000600 was deleted on conflict, resulting in the loss of
      // SDK-generated data
      REQUIRE(diagnostics.warning.size() == 1);
      REQUIRE(
          diagnostics.warning[0].find(
              "On consent-level storage directory migration, resolved filename "
              "conflict by deleting source file"
          ) != std::string::npos
      );
      REQUIRE(diagnostics.warning[0].find("444") != std::string::npos);
      REQUIRE(diagnostics.warning[0].find("1700000000600") != std::string::npos);
    }

    // Storage initialization runs a ton of different filesystem operations, all of
    // which can fail. These tests attempt to cover all practical error paths to ensure
    // that the SDK responds as expected to all filesystem conditions.
    SECTION("{error handling}") {
      // === _fs.ListSubdirectories() failure cases ===

      SECTION(
          "M initialize OK but fail migration with warning W directory listing fails "
          "in <instance>/"
      ) {
        // Given a filesystem that refuses to return directory listings for the
        // configured instance-level storage directory
        fs.SimulateFailure(
            "app/.datadog/main",
            FilesystemResult::UnknownError,
            MockFilesystemNew::FailureFlags::Ls
        );

        // When we attempt to initialize SDK storage
        SdkStorage storage(fs, logger, 555);
        const bool ok = storage.Initialize("app", "main");

        // Then storage initialization for PID 555 succeeds without migrating any data
        REQUIRE(ok);
        REQUIRE(fs.IsDirectory("app/.datadog/main/333"));
        REQUIRE(fs.IsDirectory("app/.datadog/main/444"));
        REQUIRE(fs.IsDirectory("app/.datadog/main/555"));
        REQUIRE(!fs.IsDirectory("app/.datadog/main/555/bar"));

        // And we get two warnings indicating that migration failed because of the
        // inability to list existing directories (this happens twice because we scan
        // twice, first for the initial migrate-via-rename optimization, then again for
        // the file-by-file migration attempt)
        REQUIRE(diagnostics.warning.size() == 2);
        RequireFilesystemError(
            diagnostics.warning[0],
            "UnknownError",
            "app/.datadog/main",
            "unable to list subdirectories"
        );
        RequireFilesystemError(
            diagnostics.warning[1],
            "UnknownError",
            "app/.datadog/main",
            "unable to list subdirectories"
        );
      }

      SECTION(
          "M initialize OK but fail migration with warning W directory listing fails "
          "in <instance>/<pid>/"
      ) {
        // Given a filesystem that refuses to return directory listings for the
        // process-level directory of an abandoned process for which we attempt a
        // file-by-file migration
        fs.SimulateFailure(
            "app/.datadog/main/444",
            FilesystemResult::UnknownError,
            MockFilesystemNew::FailureFlags::Ls
        );

        // When we attempt to initialize SDK storage
        SdkStorage storage(fs, logger, 555);
        const bool ok = storage.Initialize("app", "main");

        // Then storage initialization for PID 555 succeeds, claiming the data from PID
        // 333 but failing to migrate data from PID 444
        REQUIRE(ok);
        REQUIRE(!fs.IsDirectory("app/.datadog/main/333"));
        REQUIRE(fs.IsDirectory("app/.datadog/main/444"));
        REQUIRE(fs.IsDirectory("app/.datadog/main/555"));
        REQUIRE(fs.IsDirectory("app/.datadog/main/555/bar"));

        // And we get a warning indicating that migration of PID 444's data failed due
        // to a failure to list directories
        REQUIRE(diagnostics.warning.size() == 1);
        RequireFilesystemError(
            diagnostics.warning[0],
            "UnknownError",
            "app/.datadog/main/444",
            "unable to list subdirectories"
        );
      }

      SECTION(
          "M initialize OK but fail migration with warning W directory listing fails "
          "in <instance>/<pid>/<feature>/"
      ) {
        // Given a filesystem that refuses to return directory listings for the
        // feature-level directory 'foo' of an abandoned process for which we attempt a
        // file-by-file migration
        fs.SimulateFailure(
            "app/.datadog/main/444/foo",
            FilesystemResult::UnknownError,
            MockFilesystemNew::FailureFlags::Ls
        );

        // When we attempt to initialize SDK storage
        SdkStorage storage(fs, logger, 555);
        const bool ok = storage.Initialize("app", "main");

        // Then storage initialization for PID 555 succeeds, claiming the data from PID
        // 333 but failing to migrate data from feature 'bar' in PID 444
        REQUIRE(ok);
        REQUIRE(!fs.IsDirectory("app/.datadog/main/333"));
        REQUIRE(fs.IsDirectory("app/.datadog/main/444"));
        REQUIRE(fs.IsDirectory("app/.datadog/main/555"));
        REQUIRE(fs.IsDirectory("app/.datadog/main/555/bar"));

        // But the result is a partial migration: i.e. data was migrated from 444/bar/,
        // and the failure didn't occur until later, when attempting to migrate from
        // 444/foo/. In practice, it's very unlikely that permissions/accesibility would
        // differ between two feature-level directories on the same volume, so we accept
        // the possibility of partial migration and data loss in rare cases like these.
        REQUIRE(fs.Cat("app/.datadog/main/555/foo/v1/1700000000030") == "333-foo-1");
        REQUIRE(!fs.IsFile("app/.datadog/main/555/foo/intermediate-v1/1700000000000"));
        REQUIRE(fs.Cat("app/.datadog/main/555/bar/v1/1700000000530") == "333-bar-1");
        REQUIRE(fs.Cat("app/.datadog/main/555/bar/v1/1700000000500") == "444-bar-1");

        // And we get a warning indicating that migration of PID 444's data failed due
        // to a failure to list directories
        REQUIRE(diagnostics.warning.size() == 1);
        RequireFilesystemError(
            diagnostics.warning[0],
            "UnknownError",
            "app/.datadog/main/444/foo",
            "unable to list subdirectories"
        );
      }

      // === _fs.ListFiles() failure cases ===

      SECTION(
          "M initialize OK but fail migration with warning W file listing fails "
          "in <instance>/<pid>/<feature>/<consent>/"
      ) {
        // Given a filesystem that refuses to list files in a consent-level directory
        fs.SimulateFailure(
            "app/.datadog/main/444/bar/v1",
            FilesystemResult::UnknownError,
            MockFilesystemNew::FailureFlags::Ls
        );

        // When we attempt to initialize SDK storage
        SdkStorage storage(fs, logger, 555);
        const bool ok = storage.Initialize("app", "main");

        // Then storage initialization for PID 555 succeeds, but migration is partial,
        // failing on the attempt to migrate from 444/bar/v1
        REQUIRE(ok);
        REQUIRE(!fs.IsDirectory("app/.datadog/main/333"));
        REQUIRE(fs.IsDirectory("app/.datadog/main/444"));
        REQUIRE(fs.IsDirectory("app/.datadog/main/555"));
        REQUIRE(fs.IsDirectory("app/.datadog/main/555/bar/v1"));
        REQUIRE(fs.Cat("app/.datadog/main/555/bar/v1/1700000000530") == "333-bar-1");
        REQUIRE(!fs.IsFile("app/.datadog/main/555/bar/v1/1700000000500"));
        REQUIRE(fs.IsFile("app/.datadog/main/444/bar/v1/1700000000500"));

        // And we get a warning indicating that migration of PID 444's data failed due
        // to a failure to list files
        REQUIRE(diagnostics.warning.size() == 1);
        RequireFilesystemError(
            diagnostics.warning[0],
            "UnknownError",
            "app/.datadog/main/444/bar/v1",
            "unable to list files"
        );
      }

      // === _fs.Rename() failure cases ===

      SECTION(
          "M initialize OK but fail migration-via-rename W directory rename fails"
      ) {
        // Given a filesystem that refuses to allow main/333 to be relocated (or
        // accessed at all)
        fs.SimulateFailure(
            "app/.datadog/main/333",
            FilesystemResult::PermissionDenied,
            MockFilesystemNew::FailureFlags::All
        );

        // When we attempt to initialize SDK storage
        SdkStorage storage(fs, logger, 555);
        const bool ok = storage.Initialize("app", "main");

        // Then initialization is OK, but data from PID 333 is left in place, while data
        // from PID 444 is migrated without issue
        REQUIRE(ok);
        REQUIRE(fs.Cat("app/.datadog/main/555/bar/v1/1700000000500") == "444-bar-1");
        REQUIRE(!fs.IsFile("app/.datadog/main/555/bar/v1/1700000000530"));
        REQUIRE(fs.Cat("app/.datadog/main/333/bar/v1/1700000000530") == "333-bar-1");

        // And we get a warning indicating that migration of PID 333's data failed due
        // to a rename failure
        REQUIRE(diagnostics.warning.size() == 2);
        RequireFilesystemError(
            diagnostics.warning[0],
            "PermissionDenied",
            "app/.datadog/main/333",
            "rename failed"
        );

        // And another warning indicates that file-by-file migration of PID 333's data
        // also failed
        RequireFilesystemError(
            diagnostics.warning[1],
            "PermissionDenied",
            "app/.datadog/main/333",
            "unable to list subdirectories"
        );
      }

      SECTION(
          "M initialize OK but fail file-by-file migration W file rename fails on "
          "batch file"
      ) {
        // Given a filesystem that refuses to allow a particular batch of event data
        // within main/444 to be relocated
        fs.SimulateFailure(
            "app/.datadog/main/444/bar/v1/1700000000600",
            FilesystemResult::OutOfSpace,
            MockFilesystemNew::FailureFlags::Rename
        );

        // When we attempt to initialize SDK storage
        SdkStorage storage(fs, logger, 555);
        const bool ok = storage.Initialize("app", "main");

        // Then init is OK, we get PID 333's data, but PID 444's data is not fully
        // migrated
        REQUIRE(ok);
        REQUIRE(fs.Cat("app/.datadog/main/555/bar/v1/1700000000530") == "333-bar-1");
        REQUIRE(!fs.IsFile("app/.datadog/main/555/bar/v1/1700000000600"));
        REQUIRE(fs.Cat("app/.datadog/main/444/bar/v1/1700000000600") == "444-bar-2");

        // And this is a partial merge, because the error occurs partway through the
        // file-by-file process: as described elsewhere, this is unlikely in practice
        // and so we accept the rare possibility of partial migration
        REQUIRE(fs.Cat("app/.datadog/main/555/bar/v1/1700000000500") == "444-bar-1");
        REQUIRE(!fs.IsFile("app/.datadog/main/444/bar/v1/1700000000500"));

        // And a warning indicates the reason for the failed migration of PID 444's
        // 'bar' event data
        REQUIRE(diagnostics.warning.size() == 1);
        RequireFilesystemError(
            diagnostics.warning[0],
            "OutOfSpace",
            "app/.datadog/main/444/bar/v1/1700000000600",
            "unable to rename file"
        );
      }

      // === _fs.Delete() failure cases ===

      SECTION(
          "M initialize OK but log a warning W delete fails on lockfile for data "
          "migrated via directory rename"
      ) {
        // Given a filesystem that refuses to allow the lockfile for PID 333 to be
        // deleted
        fs.SimulateFailure(
            "app/.datadog/main/333.lock",
            FilesystemResult::ReadOnlyFilesystem,
            MockFilesystemNew::FailureFlags::Delete
        );

        // When we initialize SDK storage successfully
        SdkStorage storage(fs, logger, 555);
        REQUIRE(storage.Initialize("app", "main"));

        // Then migration is completed successfully, but the original lockfile remains
        // in place since we couldn't delete it post-migrate
        REQUIRE(fs.Cat("app/.datadog/main/555/bar/v1/1700000000500") == "444-bar-1");
        REQUIRE(fs.Cat("app/.datadog/main/555/bar/v1/1700000000530") == "333-bar-1");
        REQUIRE(!fs.IsFile("app/.datadog/main/444.lock"));
        REQUIRE(fs.IsFile("app/.datadog/main/333.lock"));
        REQUIRE(!fs.IsDirectory("app/.datadog/main/333"));

        // And we get a warning about the failed delete
        REQUIRE(diagnostics.warning.size() == 1);
        RequireFilesystemError(
            diagnostics.warning[0],
            "ReadOnlyFilesystem",
            "app/.datadog/main/333.lock",
            "Failed to delete old-process lockfile"
        );
      }

      SECTION(
          "M initialize OK but log a warning W delete fails on lockfile for data "
          "migrated file-by-file"
      ) {
        // Given a filesystem that refuses to allow the lockfile for PID 444 to be
        // deleted
        fs.Touch("app/.datadog/main/444.lock");
        fs.SimulateFailure(
            "app/.datadog/main/444.lock",
            FilesystemResult::ReadOnlyFilesystem,
            MockFilesystemNew::FailureFlags::Delete
        );

        // When we initialize SDK storage successfully
        SdkStorage storage(fs, logger, 555);
        REQUIRE(storage.Initialize("app", "main"));

        // Then migration is completed successfully, but the original lockfile remains
        // in place since we couldn't delete it post-migrate
        REQUIRE(fs.Cat("app/.datadog/main/555/bar/v1/1700000000500") == "444-bar-1");
        REQUIRE(fs.Cat("app/.datadog/main/555/bar/v1/1700000000530") == "333-bar-1");
        REQUIRE(!fs.IsFile("app/.datadog/main/333.lock"));
        REQUIRE(fs.IsFile("app/.datadog/main/444.lock"));
        REQUIRE(!fs.IsDirectory("app/.datadog/main/444"));

        // And we get a warning about the failed delete
        REQUIRE(diagnostics.warning.size() == 1);
        RequireFilesystemError(
            diagnostics.warning[0],
            "ReadOnlyFilesystem",
            "app/.datadog/main/444.lock",
            "Failed to delete old-process lockfile"
        );
      }

      SECTION(
          "M initialize OK but log a warning W delete fails on batch file with name "
          "conflict"
      ) {
        // Given a batch file that was written to PID 333's storage with the exact same
        // timestamp as an existing batch in PID 444
        fs.Touch("app/.datadog/main/333/bar/v1/1700000000600", "333-bar-2");
        REQUIRE(fs.Cat("app/.datadog/main/444/bar/v1/1700000000600") == "444-bar-2");

        // And a filesystem that will not allow the file from 444 to be deleted
        fs.SimulateFailure(
            "app/.datadog/main/444/bar/v1/1700000000600",
            FilesystemResult::UnknownError,
            MockFilesystemNew::FailureFlags::Delete
        );

        // When we initialize SDK storage, causing a file-by-file migration of main/444
        // into main/555 (renamed from main/333)
        SdkStorage storage(fs, logger, 555);
        REQUIRE(storage.Initialize("app", "main"));

        // Then migration is completed partially: when the SDK fails to delete the
        // conflicting src_file, it aborts the migration since it knows it won't be able
        // to delete the directory for PID 444
        REQUIRE(fs.Cat("app/.datadog/main/555/bar/v1/1700000000500") == "444-bar-1");
        REQUIRE(fs.Cat("app/.datadog/main/555/bar/v1/1700000000530") == "333-bar-1");
        REQUIRE(fs.Cat("app/.datadog/main/555/bar/v1/1700000000600") == "333-bar-2");
        REQUIRE(fs.Cat("app/.datadog/main/444/bar/v1/1700000000600") == "444-bar-2");

        // And we get a warning about the failed delete
        REQUIRE(diagnostics.warning.size() == 1);
        RequireFilesystemError(
            diagnostics.warning[0],
            "UnknownError",
            "app/.datadog/main/444/bar/v1/1700000000600",
            "unable to delete file"
        );
      }

      SECTION("M initialize OK but log a warning W delete fails on loose files") {
        // Given a loose file that exists in either the process-level or feature-level
        // directories for an abandoned process that will be migrated file-by-file
        auto path = GENERATE(
            "app/.datadog/main/444/Thumbs.db", "app/.datadog/main/444/foo/Thumbs.db"
        );
        fs.Touch(path);

        // And a filesystem that will not allow that file to be deleted
        fs.SimulateFailure(
            path,
            FilesystemResult::UnknownError,
            MockFilesystemNew::FailureFlags::Delete
        );

        // When we initialize SDK storage successfully
        SdkStorage storage(fs, logger, 555);
        REQUIRE(storage.Initialize("app", "main"));

        // Then migration is technically incomplete, as the source directory could not
        // be deleted (since it's not empty)
        REQUIRE(fs.IsDirectory("app/.datadog/main/444"));

        // And we get a warning about the failed delete
        REQUIRE(diagnostics.warning.size() == 1);
        RequireFilesystemError(
            diagnostics.warning[0], "UnknownError", path, "unable to delete loose file"
        );
      }

      // === _fs.DeleteDirectory() failure cases ===

      SECTION("M initialize OK but log a warning W delete fails on empty directories") {
        // Given a filesystem that will not allow a specific directory to be deleted
        auto path = GENERATE(
            "app/.datadog/main/444/bar/v1",
            "app/.datadog/main/444/bar",
            "app/.datadog/main/444"
        );
        fs.SimulateFailure(
            path,
            FilesystemResult::UnknownError,
            MockFilesystemNew::FailureFlags::Delete
        );

        // When we initialize SDK storage successfully
        SdkStorage storage(fs, logger, 555);
        REQUIRE(storage.Initialize("app", "main"));

        // Then migration is at least partially successful, but the relevant source dir
        // remains on disk
        fs.IsFile("app/.datadog/main/555/bar/v1/1700000000600");
        fs.IsDirectory(path);

        // And we get a warning about the failed delete
        REQUIRE(diagnostics.warning.size() == 1);
        RequireFilesystemError(
            diagnostics.warning[0],
            "UnknownError",
            path,
            "unable to delete directory after moving files"
        );
      }

      // === _fs.OpenForWrite() (lockfile) failure cases ===

      SECTION(
          "M log a warning W lockfile acquisition fails for a reason other than "
          "LockContention"
      ) {
        // Given an existing lockfile for either PID 333 or PID 444, unclaimed by any
        // other process
        auto path =
            GENERATE("app/.datadog/main/444.lock", "app/.datadog/main/333.lock");
        fs.Touch(path);

        // And a filesystem that will not permit that lockfile to be opened
        fs.SimulateFailure(
            path,
            FilesystemResult::ReadOnlyFilesystem,
            MockFilesystemNew::FailureFlags::Open
        );

        // When we initialize SDK storage
        SdkStorage storage(fs, logger, 555);
        const bool ok = storage.Initialize("app", "main");

        // Then initialization is still successful, and we were able to migrate event
        // data from the process whose lockfile was accessible, but the other process's
        // data is entirely untouched
        REQUIRE(ok);
        bool locked_333 = std::string_view{path}.find("333") != std::string_view::npos;
        if (locked_333) {
          REQUIRE(fs.IsDirectory("app/.datadog/main/333"));
          REQUIRE(fs.Cat("app/.datadog/main/555/bar/v1/1700000000500") == "444-bar-1");
          REQUIRE(!fs.IsFile("app/.datadog/main/555/bar/v1/1700000000530"));
        } else {
          REQUIRE(fs.IsDirectory("app/.datadog/main/444"));
          REQUIRE(fs.Cat("app/.datadog/main/555/bar/v1/1700000000530") == "333-bar-1");
          REQUIRE(!fs.IsFile("app/.datadog/main/555/bar/v1/1700000000500"));
        }

        // And we get a warning about the failure to open the lockfile
        REQUIRE(diagnostics.warning.size() > 0);
        RequireFilesystemError(
            diagnostics.warning[0],
            "ReadOnlyFilesystem",
            path,
            "unable to open lockfile for write"
        );
      }
    }
  }
}
