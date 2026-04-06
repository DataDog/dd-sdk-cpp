// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/storage/feature_event.hpp"

#include "datadog/impl/storage/sdk.hpp"

#include "mock/filesystem_new.hpp"
#include "support/catch.hpp"
#include "support/diagnostics.hpp"
#include "support/filesystem.hpp"

using namespace datadog::impl;

TEST_CASE("FeatureEventStorage paths", "[unit][storage]") {
  // Given a platform-appropriate root application storage path, and the expected prefix
  // for our feature's event storage
#ifdef _WIN32
  std::string root = "C:/Users/jqpublic/AppData/Local/someapp";
  std::string want_prefix = root + "\\.datadog\\main\\555\\foo\\";
#else
  std::string root = "/home/jqpublic/.local/share/someapp";
  std::string want_prefix = root + "/.datadog/main/555/foo/";
#endif

  // And a successfully-initialized a FeatureEventStorage instance within that root dir
  MockFilesystemNew fs;
  fs.Mkdirs(root);
  DiagnosticMessageBuffer diagnostics;
  DiagnosticLogger logger = diagnostics.CreateTestLogger();
  SdkStorage storage(fs, logger, 555);
  REQUIRE(storage.Initialize(root, "main"));
  auto events = storage.InitializeFeatureEventStorage("foo");
  REQUIRE(events != nullptr);

  SECTION("M return valid intermediate event-batch path W GetPendingPath called") {
    // When we call GetPendingPath()
    auto path = events->GetPendingPath();

    // Then we get a valid path, relative to our root storage directory, to the
    // directory that's used for events which can't yet be uploaded
    REQUIRE(path.Get() == want_prefix + "intermediate-v1");
  }

  SECTION("M return valid upload-eligible event-batch path W GetGrantedPath called") {
    // When we call GetGrantedPath()
    auto path = events->GetGrantedPath();

    // Then we get a valid path, relative to our root storage directory, to the
    // directory that's used for events which we have consent to upload
    REQUIRE(path.Get() == want_prefix + "v1");
  }
}

TEST_CASE("FeatureEventStorage event migration", "[unit][storage]") {
  // Given a filesystem with application files stored in app/
  MockFilesystemNew fs;
  fs.Mkdirs("app");

  // And a diagnostic logger that will capture all messages emitted
  DiagnosticMessageBuffer diagnostics;
  DiagnosticLogger logger = diagnostics.CreateTestLogger();

  // And properly-initialized SdkStorage instance that will store events in
  // app/.datadog/main/555/
  SdkStorage storage(fs, logger, 555);
  REQUIRE(storage.Initialize("app", "main"));

  // And a properly-initialized FeatureEventStorage instance that will manage batch
  // files stored within app/.datadog/main/555/foo/
  auto events = storage.InitializeFeatureEventStorage("foo");
  REQUIRE(events != nullptr);

  SECTION("DeletePendingBatches") {
    SECTION("M delete all batch files W pending directory contains batch files") {
      // Given two batch files in the pending dir
      fs.Touch("app/.datadog/main/555/foo/intermediate-v1/1700000000000", "batch-0");
      fs.Touch("app/.datadog/main/555/foo/intermediate-v1/1700000000100", "batch-1");

      // When we delete pending batches
      const bool ok = events->DeletePendingBatches();

      // Then the operation succeeds
      REQUIRE(ok);

      // And both batch files are gone
      REQUIRE(!fs.IsFile("app/.datadog/main/555/foo/intermediate-v1/1700000000000"));
      REQUIRE(!fs.IsFile("app/.datadog/main/555/foo/intermediate-v1/1700000000100"));

      // And no errors or warnings were logged
      REQUIRE(diagnostics.error.size() == 0);
      REQUIRE(diagnostics.warning.size() == 0);
    }

    SECTION("M skip non-numeric files W pending directory contains non-SDK files") {
      // Given a pending directory containing one SDK-written batch file and one file
      // with a non-numeric name (i.e. not written by the SDK)
      fs.Touch("app/.datadog/main/555/foo/intermediate-v1/1700000000000", "batch-0");
      fs.Touch("app/.datadog/main/555/foo/intermediate-v1/Thumbs.db", "not-sdk-data");

      // When we delete pending batches
      const bool ok = events->DeletePendingBatches();

      // Then the operation succeeds
      REQUIRE(ok);

      // And only the numerically-named SDK file is deleted
      REQUIRE(!fs.IsFile("app/.datadog/main/555/foo/intermediate-v1/1700000000000"));

      // And the non-SDK file is left untouched
      REQUIRE(fs.IsFile("app/.datadog/main/555/foo/intermediate-v1/Thumbs.db"));

      // And no errors or warnings were logged
      REQUIRE(diagnostics.error.size() == 0);
      REQUIRE(diagnostics.warning.size() == 0);
    }

    SECTION("M return true W pending directory is empty") {
      // Given no files in the pending directory

      // When we delete pending batches
      const bool ok = events->DeletePendingBatches();

      // Then the operation succeeds
      REQUIRE(ok);

      // And no errors or warnings were logged
      REQUIRE(diagnostics.error.size() == 0);
      REQUIRE(diagnostics.warning.size() == 0);
    }

    SECTION("M return false W listing the pending directory fails") {
      // Given a pending directory with a file in it
      fs.Touch("app/.datadog/main/555/foo/intermediate-v1/1700000000000", "batch-0");

      // And a filesystem that refuses to list files in the pending directory
      fs.SimulateFailure(
          "app/.datadog/main/555/foo/intermediate-v1",
          FilesystemResult::PermissionDenied,
          MockFilesystemNew::FailureFlags::Ls
      );

      // When we attempt to delete pending batches
      const bool ok = events->DeletePendingBatches();

      // Then the operation fails
      REQUIRE(!ok);

      // And the batch file is untouched
      REQUIRE(fs.IsFile("app/.datadog/main/555/foo/intermediate-v1/1700000000000"));

      // And an error is logged identifying the listing failure
      REQUIRE(diagnostics.error.size() == 1);
      RequireFilesystemError(
          diagnostics.error[0],
          "PermissionDenied",
          "app/.datadog/main/555/foo/intermediate-v1",
          "directory listing failed"
      );
    }

    SECTION("M return false W deleting a batch file fails") {
      // Given an initialized FeatureEventStorage with two batch files in the pending
      // dir
      fs.Touch("app/.datadog/main/555/foo/intermediate-v1/1700000000000", "batch-0");
      fs.Touch("app/.datadog/main/555/foo/intermediate-v1/1700000000100", "batch-1");

      // And a filesystem that refuses to delete the first batch file
      fs.SimulateFailure(
          "app/.datadog/main/555/foo/intermediate-v1/1700000000000",
          FilesystemResult::PermissionDenied,
          MockFilesystemNew::FailureFlags::Delete
      );

      // When we attempt to delete pending batches
      const bool ok = events->DeletePendingBatches();

      // Then the operation fails
      REQUIRE(!ok);

      // And the undeletable file remains on disk
      REQUIRE(fs.IsFile("app/.datadog/main/555/foo/intermediate-v1/1700000000000"));

      // And an error is logged identifying the failing path
      REQUIRE(diagnostics.error.size() == 1);
      RequireFilesystemError(
          diagnostics.error[0],
          "PermissionDenied",
          "app/.datadog/main/555/foo/intermediate-v1/1700000000000",
          "Could not delete batch file on consent change"
      );
    }
  }

  SECTION("MigratePendingBatchesToGranted") {
    SECTION(
        "M move all batch files to the granted directory W pending contains batches"
    ) {
      // Given an initialized FeatureEventStorage with two batch files in the pending
      // dir
      fs.Touch("app/.datadog/main/555/foo/intermediate-v1/1700000000000", "batch-0");
      fs.Touch("app/.datadog/main/555/foo/intermediate-v1/1700000000100", "batch-1");

      // When we migrate pending batches to the granted directory
      const bool ok = events->MigratePendingBatchesToGranted();

      // Then the operation succeeds
      REQUIRE(ok);

      // And the pending directory is now empty
      REQUIRE(!fs.IsFile("app/.datadog/main/555/foo/intermediate-v1/1700000000000"));
      REQUIRE(!fs.IsFile("app/.datadog/main/555/foo/intermediate-v1/1700000000100"));

      // And the granted directory now contains the relocated batch files
      REQUIRE(fs.Cat("app/.datadog/main/555/foo/v1/1700000000000") == "batch-0");
      REQUIRE(fs.Cat("app/.datadog/main/555/foo/v1/1700000000100") == "batch-1");

      // And no errors or warnings were logged
      REQUIRE(diagnostics.error.size() == 0);
      REQUIRE(diagnostics.warning.size() == 0);
    }

    SECTION("M skip non-numeric files W pending directory contains non-SDK files") {
      // Given a pending directory with one SDK batch file and one non-SDK file
      fs.Touch("app/.datadog/main/555/foo/intermediate-v1/1700000000000", "batch-0");
      fs.Touch("app/.datadog/main/555/foo/intermediate-v1/Thumbs.db", "not-sdk-data");

      // When we migrate pending batches
      const bool ok = events->MigratePendingBatchesToGranted();

      // Then the operation succeeds
      REQUIRE(ok);

      // And only the numerically-named batch is moved
      REQUIRE(fs.Cat("app/.datadog/main/555/foo/v1/1700000000000") == "batch-0");

      // And the non-SDK file is left in the pending directory
      REQUIRE(fs.IsFile("app/.datadog/main/555/foo/intermediate-v1/Thumbs.db"));
      REQUIRE(!fs.IsFile("app/.datadog/main/555/foo/v1/Thumbs.db"));

      // And no errors or warnings were logged
      REQUIRE(diagnostics.error.size() == 0);
      REQUIRE(diagnostics.warning.size() == 0);
    }

    SECTION("M return true W pending directory is empty") {
      // Given an initialized FeatureEventStorage with no files in the pending directory

      // When we migrate pending batches
      const bool ok = events->MigratePendingBatchesToGranted();

      // Then the operation succeeds (vacuously)
      REQUIRE(ok);

      // And no errors or warnings were logged
      REQUIRE(diagnostics.error.size() == 0);
      REQUIRE(diagnostics.warning.size() == 0);
    }

    SECTION(
        "M delete the pending copy and preserve the granted copy W a filename conflict "
        "exists"
    ) {
      // Given a granted directory that already contains a file named '1700000000000'
      fs.Touch("app/.datadog/main/555/foo/v1/1700000000000", "old-granted");

      // And a pending directory that also contains a file by that same name
      fs.Touch(
          "app/.datadog/main/555/foo/intermediate-v1/1700000000000", "new-pending"
      );

      // And a second pending file with a non-conflicting name that should migrate
      // normally
      fs.Touch("app/.datadog/main/555/foo/intermediate-v1/1700000000100", "batch-1");

      // When we migrate pending batches
      const bool ok = events->MigratePendingBatchesToGranted();

      // Then the operation succeeds
      REQUIRE(ok);

      // And the pending directory is now empty
      REQUIRE(!fs.IsFile("app/.datadog/main/555/foo/intermediate-v1/1700000000000"));
      REQUIRE(!fs.IsFile("app/.datadog/main/555/foo/intermediate-v1/1700000000100"));

      // And the pre-existing granted file is left untouched (pending copy was
      // discarded)
      REQUIRE(fs.Cat("app/.datadog/main/555/foo/v1/1700000000000") == "old-granted");

      // And the non-conflicting pending file was migrated normally
      REQUIRE(fs.Cat("app/.datadog/main/555/foo/v1/1700000000100") == "batch-1");

      // And a warning was logged about the conflict resolution
      REQUIRE(diagnostics.error.size() == 0);
      REQUIRE(diagnostics.warning.size() == 1);
      REQUIRE(
          diagnostics.warning[0].find(
              "Deleted pending-directory copy of duplicate batch file"
          ) != std::string::npos
      );
    }

    SECTION(
        "M log a warning and continue W filename conflict exists and pending copy "
        "cannot be deleted"
    ) {
      // Given a filename conflict between the pending and granted directories
      fs.Touch("app/.datadog/main/555/foo/v1/1700000000000", "old-granted");
      fs.Touch(
          "app/.datadog/main/555/foo/intermediate-v1/1700000000000", "new-pending"
      );

      // And a second pending file with a non-conflicting name
      fs.Touch("app/.datadog/main/555/foo/intermediate-v1/1700000000100", "batch-1");

      // And a filesystem that refuses to delete the conflicting pending file
      fs.SimulateFailure(
          "app/.datadog/main/555/foo/intermediate-v1/1700000000000",
          FilesystemResult::PermissionDenied,
          MockFilesystemNew::FailureFlags::Delete
      );

      // When we migrate pending batches
      const bool ok = events->MigratePendingBatchesToGranted();

      // Then the operation still succeeds overall: a failed conflict resolution is
      // treated as a non-fatal warning and migration continues
      REQUIRE(ok);

      // And the conflicting pending file is left in place (since we couldn't delete it)
      REQUIRE(fs.IsFile("app/.datadog/main/555/foo/intermediate-v1/1700000000000"));

      // And the pre-existing granted file is preserved
      REQUIRE(fs.Cat("app/.datadog/main/555/foo/v1/1700000000000") == "old-granted");

      // And the non-conflicting file is still migrated successfully
      REQUIRE(fs.Cat("app/.datadog/main/555/foo/v1/1700000000100") == "batch-1");
      REQUIRE(!fs.IsFile("app/.datadog/main/555/foo/intermediate-v1/1700000000100"));

      // And a warning is logged about the failed conflict resolution
      REQUIRE(diagnostics.error.size() == 0);
      REQUIRE(diagnostics.warning.size() == 1);
      RequireFilesystemError(
          diagnostics.warning[0],
          "PermissionDenied",
          "app/.datadog/main/555/foo/intermediate-v1/1700000000000",
          "Could not delete pending-directory copy of duplicate batch file"
      );
    }

    SECTION("M return false W listing the pending directory fails") {
      // Given an initialized FeatureEventStorage with a batch file in the pending dir
      fs.Touch("app/.datadog/main/555/foo/intermediate-v1/1700000000000", "batch-0");

      // And a filesystem that refuses to list files in the pending directory
      fs.SimulateFailure(
          "app/.datadog/main/555/foo/intermediate-v1",
          FilesystemResult::PermissionDenied,
          MockFilesystemNew::FailureFlags::Ls
      );

      // When we attempt to migrate pending batches
      const bool ok = events->MigratePendingBatchesToGranted();

      // Then the operation fails
      REQUIRE(!ok);

      // And the batch file remains in the pending directory, unmoved
      REQUIRE(fs.IsFile("app/.datadog/main/555/foo/intermediate-v1/1700000000000"));
      REQUIRE(!fs.IsFile("app/.datadog/main/555/foo/v1/1700000000000"));

      // And an error is logged identifying the listing failure
      REQUIRE(diagnostics.error.size() == 1);
      RequireFilesystemError(
          diagnostics.error[0],
          "PermissionDenied",
          "app/.datadog/main/555/foo/intermediate-v1",
          "directory listing failed"
      );
    }

    SECTION("M return false W a rename fails for a reason other than AlreadyExists") {
      // Given an initialized FeatureEventStorage with two batch files in the pending
      // dir
      fs.Touch("app/.datadog/main/555/foo/intermediate-v1/1700000000000", "batch-0");
      fs.Touch("app/.datadog/main/555/foo/intermediate-v1/1700000000100", "batch-1");

      // And a filesystem that refuses to rename the first batch file
      fs.SimulateFailure(
          "app/.datadog/main/555/foo/intermediate-v1/1700000000000",
          FilesystemResult::OutOfSpace,
          MockFilesystemNew::FailureFlags::Rename
      );

      // When we attempt to migrate pending batches
      const bool ok = events->MigratePendingBatchesToGranted();

      // Then the operation fails
      REQUIRE(!ok);

      // And the undeletable file remains in the pending directory
      REQUIRE(fs.IsFile("app/.datadog/main/555/foo/intermediate-v1/1700000000000"));
      REQUIRE(!fs.IsFile("app/.datadog/main/555/foo/v1/1700000000000"));

      // And an error is logged identifying the rename failure
      REQUIRE(diagnostics.error.size() == 1);
      RequireFilesystemError(
          diagnostics.error[0],
          "OutOfSpace",
          "app/.datadog/main/555/foo/intermediate-v1/1700000000000",
          "rename failed"
      );
    }
  }
}
