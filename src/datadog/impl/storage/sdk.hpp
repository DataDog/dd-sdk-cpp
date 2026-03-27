// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include "datadog/impl/diagnostics.hpp"
#include "datadog/impl/storage/filesystem.hpp"
#include "datadog/impl/storage/path.hpp"

namespace datadog::impl {

/**
 * Root storage interface owned by a single SDK instance.
 *
 * Basic constraints of on-disk storage:
 *
 * - Each application is configured with a dedicated "application storage" directory.
 * - Within this directory, a .datadog/ subdirectory contains all SDK-generated files.
 *
 * An application _may_ use multiple instances of the SDK. In typical usage, only a
 * single instance named "main" is present, but others may exist. This gives each SDK
 * instance its own dedicated root storage directory:
 *
 * - <application-storage>/.datadog/<instance-name>/
 *
 * Each Core instance has its own SdkStorage interface, which is rooted at this path.
 *
 * === Global Artifact Storage ===
 *
 * The SDK is primarily concerned with storing event data, which is collected and
 * batched per-feature, segregated into SDK instance and PID directories, as described
 * in the "Event Storage" section below.
 *
 * However, some features need to store global, process-wide artifacts, such as crash
 * dumps. These files are kept in "artifact" directories (identified with dot-prefixed
 * names) directly beneath .datadog/, e.g.:
 *
 * - <application-storage>/.datadog/.crashes/
 *
 * Unlike event data, these files are not segregated by SDK instance name or PID,
 * requiring that the relevant feature take its own measures to prevent contention. For
 * example, the CrashReporting feature may only be enabled once per process, and it
 * uses filesystem locks when writing and reading files in .crashes/.
 *
 * === Event Storage ===
 *
 * Because an application might run multiple processes concurrently, each SDK instance
 * knows the PID of the process in which it's running. An SDK instance will only write
 * new data to (and upload from) a PID-specific subdirectory, and it will maintain a
 * filesystem-level lock on an accompanying lockfile in order to indicate that it's
 * still alive and actively managing the files in that directory:
 *
 * - <application-storage>/.datadog/<instance-name>/<pid>.lock
 * - <application-storage>/.datadog/<instance-name>/<pid>/
 *
 * SdkStorage maintains this lockfile and manages access to this PID-specific directory
 * for storage and upload. It's also responsible for performing migration of abandoned
 * data from other processes which are no longer running.
 *
 * Because each SDK instance operates independently, migration is performed only within
 * the relevant <instance-name> directory.
 *
 * For example, suppose process 1234 ran with two SDK instances, 'main' and 'secondary',
 * and then exited. Then process 2345 ran with only 'main', and is still running. When a
 * new process 3456 starts up, it will see:
 *
 * - <application-storage>/.datadog/main/1234.lock
 * - <application-storage>/.datadog/main/1234/
 * - <application-storage>/.datadog/secondary/1234.lock
 * - <application-storage>/.datadog/secondary/1234/
 * - <application-storage>/.datadog/main/2345.lock
 * - <application-storage>/.datadog/main/2345/
 *
 * The new process will entirely ignore the 'secondary' path. Within 'main', it will:
 *
 * - Create and acquire 3456.lock
 * - Successfully acquire 1234.lock
 * - Rename '1234' to '3456', effectively migrating data from the old process
 * - Fail to acquire 2345.lock, leaving '2345' untouched
 */
class SdkStorage {
 public:
  /**
   * Creates a new SdkStorage instance which will use the given IFilesystem interface
   * for file operations.
   */
  explicit SdkStorage(IFilesystem& fs, uint32_t pid);

  /**
   * Ensures that the current-PID lockfile is closed when this object leaves scope.
   */
  ~SdkStorage();

  bool Initialize(
      const impl::DiagnosticLogger& logger,
      std::string_view application_storage_path,
      std::string_view sdk_instance_name
  );

  /** Returns the per-PID events root: <application-storage>/.datadog/<instance>/<pid>/
   *
   * Valid only after Initialize() returns true. The returned view is stable for the
   * lifetime of this SdkStorage instance.
   */
  std::string_view GetEventsRoot() const;

 private:
  void MigrateAbandonedEvents(const DiagnosticLogger& logger);

  bool TryClaimAbandonedDirectory(std::string_view abandoned_pid);

  void HandleMigrate(std::string_view from_pid, const DiagnosticLogger& logger);

  void MigrateFeatureEvents(
      std::string_view feature_name,
      const StoragePath& from_feature_root,
      const DiagnosticLogger& logger
  );

  bool EnsureDestinationDirectoryExists(
      std::string_view feature_name, std::string_view subdir
  );

  void MigrateFilesFromSubdirectory(
      const StoragePath& from_events_dir,
      const StoragePath& to_events_dir,
      const DiagnosticLogger& logger
  );

 private:
  IFilesystem& _fs;  // Long-lived reference to filesystem interface

  uint32_t _pid;                           // Current process's PID
  std::array<char, 11> _pid_str_buffer{};  // Null-terminated string data for _pid
  std::string_view _pid_str;               // _pid as a string, for use in paths

  StoragePath _datadog_root;   // <application-storage-path>/.datadog
  StoragePath _instance_root;  // <_datadog_root>/<sdk-instance-name>
  StoragePath _process_root;   // <_instance_root>/<pid>/

  PlatformFileHandle _lockfile_handle{INVALID_FILE_HANDLE};  // <_instance_root>/<pid>.lock
};

}  // namespace datadog::impl
