// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "datadog/impl/core/storage/artifact.hpp"
#include "datadog/impl/core/storage/feature_event.hpp"
#include "datadog/impl/core/storage/filesystem.hpp"
#include "datadog/impl/core/storage/filesystem_wrapper.hpp"
#include "datadog/impl/core/storage/path.hpp"
#include "datadog/impl/core/util/diagnostics.hpp"

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
  explicit SdkStorage(IFilesystem& in_fs, DiagnosticLogger& in_logger, int64_t in_pid);

  /**
   * Given the path to application-specific storage directory and the name of the SDK
   * instance that we're storing files for, prepares the directories required for
   * on-disk storage of data for that SDK instance, while also handling migration of
   * abandoned data from old processes that are no longer running.
   *
   * sdk_instance_name must not begin with a dot. Most applications will have a single
   * SDK instance with the default name of 'main'.
   *
   * Upon successful initialization, guarantees that:
   *
   * 1. A storage directory exists at <application-storage>/.datadog/<instance>/<pid>/
   *    where the SDK will be able to create use for event storage
   *
   * 2. An accompanying file at <application-storage>/.datadog/<instance>/<pid>.lock
   *    exists, and SdkStorage holds a lock on this file for its lifetime
   *
   * 3. A best-effort attempt was made to migrate event data from other <pid>
   *    subdirectories into ours
   *
   * Returns true if all required directories were created and the lockfile was
   * successfully acquired. If migration of abandoned event data fails, warnings will be
   * logged, but initialization will continue without failing.
   */
  bool Initialize(
      std::string_view application_storage_path, std::string_view sdk_instance_name
  );

  /**
   * Prepares an arbitrarily-named subdirectory within <application-storage>/.datadog/
   * that a feature implementation can use to persist files between processes.
   *
   * directory_name must begin with '.' to differentiate artifact storage directories
   * from SDK instance names used for PID-specific event storage directories.
   *
   * Artifact storage directories come with no guarantees or constraints re: locking or
   * multi-process support - it's up to each feature to manage contention as necessary.
   * For example, we use <application-storage>/.datadog/.crashes/ to store crash dumps.
   * Only one SDK instance per process is allowed to manage crash dumps, and the Crash
   * Reporting implementation handles multi-process contention by manually acquiring
   * advisory locks on the crash dump files that it writes and reads.
   *
   * Returns a valid ArtifactStorage value if directory initialization is successful;
   * returns nullptr otherwise.
   */
  std::unique_ptr<ArtifactStorage> InitializeArtifactStorage(
      std::string_view directory_name
  );

  /**
   * Prepares a directory at <application-storage>/.datadog/<instance>/<pid>/<feature>/,
   * with separate subdirectories for consent-pending and consent-granted event storage,
   * that the core can use to store batches of event data on behalf of that feature.
   *
   * Feature-specific event directories seamlessly handle the case of multiple SDK
   * instances within the same process, along with the case of multiple processes with
   * the same SDK configuration running concurrently:
   *
   * 1. Each uniquely-named SDK instance stores events within its own subdirectory (e.g.
   *    <application-storage>/.datadog/main vs. <application-storage>/.datadog/other),
   *    with all reading/writing/migration of event data confined to an
   *    instance-specific directory.
   *
   * 2. A single SDK instance within a single process (as represented by SdkStorage)
   *    holds a lock on <instance>/<pid>.lock for as long as it's running, and
   *    similarly-configured SDK instances in _other_ processes will respect that lock,
   *    leaving any events in <instance>/<pid>/ untouched for as long any existing
   *    process still owns them.
   *
   * Returns a valid FeatureEventStorage value if directory initialization is
   * successful; returns nullptr otherwise.
   */
  std::unique_ptr<FeatureEventStorage> InitializeFeatureEventStorage(
      std::string_view feature_name
  );

 private:
  /**
   * Scans _instance_root for existing <pid>/ directories containing event data, and
   * renames the first such directory that's been abandoned, such that it now lies at
   * _process_root.
   *
   * Temporarily acquires a lock on <pid>.lock for the old-process directory, to detect
   * whether it's been abandoned. On successful directory rename, deletes <pid>.lock.
   *
   * Returns true if the directory at _process_root now exists, regardless of whether
   * that's due to a directory rename or because such a directory already existed.
   */
  bool TryInitializeProcessRootFromAbandonedProcessDirectory(FilesystemWrapper& fsw);

  /**
   * Scans _instance_root for existing <pid>/ directories containing event data, and
   * iterates through all such directories that have been abandoned, attempting to
   * migrate all abandoned batch files into _process_root.
   */
  void MigrateAbandonedEventsToProcessRoot(FilesystemWrapper& fsw);

  /**
   * Given the name of a process-level directory relative to _instance_root, populates
   * pid_subdir_path with the full path to that directory (i.e. <old-pid>/), and
   * populates pid_lockfile_path with the path to the lockfile for that directory (i.e.
   * <old-pid>.lock).
   */
  bool BuildProcessDirectoryPaths(
      std::string_view pid_subdir_name,
      StoragePath& pid_subdir_path,
      StoragePath& pid_lockfile_path
  );

  /**
   * Given the path to a <pid>.lock file, attempts to open that file for write,
   * acquiring an advisory lock for that file in the process. Handles filesystem errors
   * with appropriate diagnostic logging.
   *
   * Returns a valid File wrapper if this process now holds a lock on that file; returns
   * std::nullopt otherwise.
   */
  std::optional<File> AcquireAbandonedProcessDirectory(
      FilesystemWrapper& fsw, const StoragePath& pid_lockfile_path
  );

  /**
   * Given a process-level directory at <instance>/<old-pid>/ (for which we've already
   * acquired <instance>/<old-pid>.lock), attempts to migrate all event files from
   * <instance>/<old-pid>/<feature>/... to <instance>/<new-pid>/<feature>/..., then
   * deletes <instance>/<old-pid>/.
   */
  bool HandleProcessDirectoryMigration(
      FilesystemWrapper& fsw, const StoragePath& pid_subdir_path
  );

  /**
   * Given the name of a <feature>/ directory that exists within <instance>/<old-pid>/,
   * creates an directory at <instance>/<new-pid>/<feature>/, moves all batch files from
   * the old path to the new path (recursing exactly one level deep into consent-level
   * subdirectories), then deletes <instance>/<old-pid>/<feature>/.
   */
  bool HandleFeatureDirectoryMigration(
      FilesystemWrapper& fsw,
      const StoragePath& pid_subdir_path,
      std::string_view feature_name
  );

  /**
   * Given a source and destination path:
   * - src: <instance>/<old-pid>/<feature>/<consent>/
   * - dst: <instance>/<new-pid>/<feature>/<consent>/
   * ...ensures that the dst directory exists, then moves all files from src to dst,
   * then deletes src.
   */
  bool HandleConsentDirectoryMigration(
      FilesystemWrapper& fsw,
      const StoragePath& src_dir_path,
      const StoragePath& dst_dir_path
  );

  /**
   * Given a directory path, uses the provided vector to store the result of
   * _fs.ListFiles(), then iterates over all files found and deletes them.
   */
  bool DeleteLooseFilesInDirectory(
      FilesystemWrapper& fsw,
      const StoragePath& dir_path,
      std::vector<std::string>& out_filenames
  );

 private:
  IFilesystem& _fs;  // Long-lived reference to filesystem interface
  DiagnosticLogger& _logger;

  int64_t _pid;                            // Current process's PID
  std::array<char, 21> _pid_str_buffer{};  // Null-terminated string data for _pid
  std::string_view _pid_str;               // _pid as a string, for use in paths

  StoragePath _datadog_root;   // <application-storage-path>/.datadog
  StoragePath _instance_root;  // <_datadog_root>/<sdk-instance-name>
  StoragePath _process_root;   // <_instance_root>/<pid>/

  std::optional<File> _lockfile;  // <pid>.lock; closed via RAII on destruction
};

}  // namespace datadog::impl
