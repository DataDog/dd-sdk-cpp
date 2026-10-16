// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/storage/sdk.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>

#include "datadog/impl/core/storage/util.hpp"
#include "datadog/impl/types/assert.hpp"

namespace datadog::impl {

SdkStorage::SdkStorage(IFilesystem& in_fs, DiagnosticLogger& in_logger, int64_t in_pid)
    : _fs(in_fs), _logger(in_logger), _pid(in_pid) {}

bool SdkStorage::Initialize(
    std::string_view application_storage_path, std::string_view sdk_instance_name
) {
  // If no application storage path has been configured, we will implicitly use the
  // current working directory: log a warning to signal to the application developer
  // that they should be explicit about where they want us to store files (this warning
  // can be suppressed, using cwd explicitly, by calling SetApplicationStoragePath("."))
  if (application_storage_path.empty()) {
    _logger.Warning(
        "Events will be stored within .datadog/ in the current working directory: "
        "application should call SetApplicationStoragePath to specify a suitable "
        "application-specific directory where .datadog/ can be created"
    );
  }

  // Require that SDK instance name is a valid directory name _without_ a dot prefix, as
  // the dot prefix is used to differentiate artifact storage directories
  // (CoreConfig does not currently expose instance name as a configurable option: when
  // it does, it should validate names at the API layer to ensure they conform to this
  // expectation)
  if (sdk_instance_name.empty() || sdk_instance_name[0] == '.') {
    DATADOG_ASSERT(false, "Invalid SDK instance name");
    _logger.Error(
        "Unexpected SDK instance name: name must be non-empty and non-dot-prefixed",
        {{"sdk_instance_name", sdk_instance_name}}
    );
    return false;
  }

  // Ensure that we've been initialized with a valid process ID: on some systems,
  // negative or zero values have special meaning, but getpid()/GetCurrentProcessId()
  // should always return a positive, nonzero value
  if (_pid <= 0) {
    _logger.Error(
        "Failed to initialize SDK storage: invalid process ID", {{"pid", _pid}}
    );
    return false;
  }

  // Convert our PID to string for easy comparison and path-building
  auto res = std::to_chars(
      _pid_str_buffer.data(), _pid_str_buffer.data() + _pid_str_buffer.size() - 1, _pid
  );
  DATADOG_ASSERT(res.ec == std::errc{}, "Failed to convert int64_t PID to string");
  *res.ptr = '\0';
  _pid_str = std::string_view{_pid_str_buffer.data()};

  // Manipulate StoragePath values to build all required paths, respecting the maximum
  // path length: if the provided <application-storage> path is too long relative to
  // MAX_STORAGE_PATH_SIZE, these operations can fail
  const char* join_message =
      "Failed to initialize SDK storage from configured application storage path: path "
      "exceeds length limit";

  // _datadog_root: root SDK storage directory is <application-storage>/.datadog/: this
  // is the only directory where any SDK instance configured with a given
  // <application-storage> value will read or write files
  if (!JoinPaths(
          _datadog_root, application_storage_path, ".datadog", _logger, join_message
      )) {
    return false;
  }

  // _instance_root: <application-storage>/.datadog/<instance-name>/ contains a
  // directory for each process, containing the event data owned by each PID: this SDK
  // instance will attempt to migrate old event files from other processes, confined to
  // its own _instance_root
  if (!JoinPaths(
          _instance_root, _datadog_root.Get(), sdk_instance_name, _logger, join_message
      )) {
    return false;
  }

  // _process_root: <application-storage>/.datadog/<instance-name>/<pid>/ is where this
  // SDK instance will store event data per-feature after migration is complete
  if (!JoinPaths(
          _process_root, _instance_root.Get(), _pid_str, _logger, join_message
      )) {
    return false;
  }

  // lockfile_path: <application-storage>/.datadog/<instance-name>/<pid>.lock is the
  // path to a file that we hold an advisory lock on in order to signal that we have
  // exclusive access to _process_root
  StoragePath lockfile_path;
  lockfile_path.MustSet(_process_root);
  if (!AppendExtensionToPath(lockfile_path, ".lock", _logger, join_message)) {
    return false;
  }

  // We've built all required paths; now we can start initializing our filesystem state:
  // initialize a FilesystemWrapper that will seamlessly handle encoding of UTF-8
  // StoragePath values into platform-native representation where required
  FilesystemWrapper fsw(_fs);

  // Create <application-storage>/.datadog/ and <instance>/, or verify that they already
  // exist
  const char* mkdir_message =
      "Failed to initialize SDK storage from configured application storage path: "
      "unable to create directory";
  if (!EnsureDirectoryExists(_datadog_root, fsw, _logger, mkdir_message)) {
    return false;
  }
  if (!EnsureDirectoryExists(_instance_root, fsw, _logger, mkdir_message)) {
    return false;
  }

  // Open <instance>/<pid>.lock and hold a lock on it, to signal to other processes that
  // we manage any directory named <instance>/<pid>/ for as long as this SDK instance
  // remains alive
  const bool append = false;  // Lockfiles have no contents; clobbering is fine
  const bool hold_advisory_lock = true;
  auto open_res = fsw.OpenForWrite(lockfile_path.CStr(), append, hold_advisory_lock);

  // If we couldn't acquire <pid>.lock, we can't safely write event data to <pid>/:
  // storage initialization has failed
  if (open_res.value != FilesystemResult::OK) {
    _logger.Error(
        "Failed to initialize SDK storage: unable to acquire lockfile",
        {{"path", lockfile_path.Get()}, {"error", FilesystemResultStr(open_res.value)}}
    );
    return false;
  }

  // We successfully opened the lockfile and acquired a lock: we'll keep this file open
  // for the lifetime of this object, ensuring that we maintain exclusive access to the
  // <pid>/ directory until the SDK instance is shut down cleanly or the process dies
  _lockfile.emplace(std::move(open_res.file));

  // Finally, we'll want to ensure that <pid>/ exists so that this SDK instance can
  // write feature-specific event data there. However, we also want to migrate any
  // now-abandoned event data from _other_ processes' <pid>/ directories into ours, so
  // that we'll take over the responsibility for uploading any data that old processes
  // wrote to disk but never uploaded.

  // As an optimization, we can migrate a single old process's event data by simply
  // renaming its directory (from <instance>/<old-pid>/ to <instance>/<new-pid>/),
  // provided that we're able to acquire a lock for that old PID directory
  const bool new_pid_dir_exists =
      TryInitializeProcessRootFromAbandonedProcessDirectory(fsw);

  // Failure to migrate is not fatal: if there were no abandoned directories to claim as
  // our own, or if migration by directory rename failed, create the <pid>/ directory so
  // the SDK will have a place to store files
  if (!new_pid_dir_exists) {
    const FilesystemResult mkdir_res = fsw.CreateDirectory(_process_root.CStr());
    if (mkdir_res != FilesystemResult::OK &&
        mkdir_res != FilesystemResult::AlreadyExistsAsDirectory) {
      // If directory creation failed, we have nowhere to put event files, so storage
      // initialization must fail
      _logger.Error(
          "Failed to initialize SDK storage: unable to create process-level storage "
          "directory",
          {{"path", _process_root.Get()}, {"error", FilesystemResultStr(mkdir_res)}}
      );
      return false;
    }
  }

  // The O(1) directory rename handles the simple case where only a single process left
  // event data behind for this one to migrate. In cases where there are multiple
  // <old-pid>/ directories in an abandoned state (i.e. no other process holds
  // <old-pid>.lock), then we need to migrate all old event batches file-by-file into
  // <new-pid>/. If this process encounters errors, we log warnings and continue/abort
  // the migration process as appropriate, but SdkStorage::Initialize() itself is now
  // successful regardless of whether migration completes cleanly.
  // TODO(RUM-15284): Consider performing file-by-file migration on the storage thread
  // so it doesn't block SDK init and scale with the number of abandoned directories
  MigrateAbandonedEventsToProcessRoot(fsw);

  // _process_root and its parent directories now exist, so the SDK can now safely:
  // 1. Call InitializeArtifactStorage() for .datadog/<artifact-dir>/
  // 2. Call InitializeFeatureEventStorage() for .datadog/<instance>/<pid>/<feature>
  return true;
}

std::unique_ptr<ArtifactStorage> SdkStorage::InitializeArtifactStorage(
    std::string_view directory_name
) {
  // Initialize() must succeed before it's legal to call this function
  DATADOG_ASSERT(
      !_datadog_root.Get().empty(),
      "_datadog_root is empty on InitializeArtifactStorage"
  );

  // Defer to ArtifactStorage::Initialize()
  auto artifacts = std::make_unique<ArtifactStorage>(_fs, _logger);
  if (!artifacts->Initialize(_datadog_root.Get(), directory_name)) {
    return nullptr;
  }
  return artifacts;
}

std::unique_ptr<FeatureEventStorage> SdkStorage::InitializeFeatureEventStorage(
    std::string_view feature_name
) {
  // Initialize() must succeed before it's legal to call this function
  DATADOG_ASSERT(
      !_process_root.Get().empty(),
      "_process_root is empty on InitializeFeatureEventStorage"
  );

  // Defer to FeatureEventStorage::Initialize()
  auto feature_events = std::make_unique<FeatureEventStorage>(_fs, _logger);
  if (!feature_events->Initialize(_process_root.Get(), feature_name)) {
    return nullptr;
  }
  return feature_events;
}

bool SdkStorage::TryInitializeProcessRootFromAbandonedProcessDirectory(
    FilesystemWrapper& fsw
) {
  // This function is called after path members are initialized
  DATADOG_ASSERT(!_pid_str.empty(), "_pid_str is empty on migration");
  DATADOG_ASSERT(!_instance_root.Get().empty(), "_instance_root is empty on migration");
  DATADOG_ASSERT(!_process_root.Get().empty(), "_process_root is empty on migration");

  // Obtain a list of all subdirectories in our top-level SDK instance directory
  std::vector<std::string> subdirs;
  FilesystemResult res = fsw.ListSubdirectories(_instance_root.CStr(), subdirs);
  if (res != FilesystemResult::OK) {
    _logger.Warning(
        "Failed to perform storage migration: unable to list subdirectories in "
        "instance-level storage directory",
        {{"path", _instance_root.Get()}, {"error", FilesystemResultStr(res)}}
    );
    return false;
  }

  // Loop once to verify that there's no existing subdirectory with our own PID: if such
  // a directory already exists, we won't be able to rename another directory to carry
  // our current PID
  for (const std::string& subdir_name : subdirs) {
    if (subdir_name == _pid_str) {
      // It's entirely possible for a process to be assigned the same PID as an earlier
      // process that's no longer alive: if events from that old process are hanging
      // around, then we've already effectively reclaimed them by opening <pid>.lock
      _logger.Status(
          "Storage migration short-circuited: a process-level storage directory "
          "already exists with our PID as its name",
          {{"parent_path", _instance_root.Get()}, {"name", subdir_name}, {"pid", _pid}}
      );
      return true;
    }
  }

  // All subdirectories (if there are any) belong to foreign processes: attempt to find
  // one that's been abandoned (i.e. <pid>.lock is no longer held) so we can claim it
  // and all its event data by simply renaming the directory to reflect our own PID
  for (const std::string& subdir_name : subdirs) {
    // Only consider directories whose names are integer-only; silently ignore others
    if (!std::all_of(subdir_name.begin(), subdir_name.end(), [](unsigned char c) {
          return std::isdigit(c);
        })) {
      continue;
    }

    // Build the full path to this process-level directory and its lockfile
    StoragePath pid_subdir_path;    // <_instance_root>/<old-pid>/
    StoragePath pid_lockfile_path;  // <_instance_root>/<old-pid>.lock
    if (!BuildProcessDirectoryPaths(subdir_name, pid_subdir_path, pid_lockfile_path)) {
      // Failure to build paths means that our application-configured storage path is
      // too long relative to MAX_STORAGE_PATH_SIZE: if any given <pid>/ path is too
      // long, it's likely that they all will be
      return false;
    }

    // Attempt to open <old-pid>.lock for write, with a non-blocking attempt to acquire
    // an advisory lock on that file: if unsuccessful, move on to the next subdirectory.
    // Our `File` RAII wrapper will automatically close <old-pid>.lock upon leaving
    // scope if we don't call pid_lockfile.Close() explicitly.
    auto pid_lockfile = AcquireAbandonedProcessDirectory(fsw, pid_lockfile_path);
    if (!pid_lockfile.has_value()) {
      continue;
    }

    // We now hold <old-pid>.lock, meaning we've claimed exclusive access to the event
    // data in <old-pid>/: we can effectively handle migration of the old process's
    // event data by performing a single atomic rename of <old-pid>/ to <new-pid>/, then
    // deleting <old-pid.lock> (n.b. we already hold <new-pid>.lock at this point)

    // We want to rename our current <old-pid>/ subdir so that it's located at the
    // path already owned by this SDK, i.e. <new_pid>/
    StoragePath& src_pid_subdir_path = pid_subdir_path;
    StoragePath& dst_pid_subdir_path = _process_root;

    // Perform the directory rename (<old-pid>/ -> <new-pid>/)
    const FilesystemResult rename_res =
        fsw.Rename(src_pid_subdir_path.CStr(), dst_pid_subdir_path.CStr());
    if (rename_res != FilesystemResult::OK) {
      // If we failed to rename the directory, log a warning and move on to the next
      // subdirectory
      _logger.Warning(
          "Ignoring process-level directory for storage migration: rename failed",
          {{"src_path", src_pid_subdir_path.Get()},
           {"dst_path", dst_pid_subdir_path.Get()},
           {"error", FilesystemResultStr(rename_res)}}
      );
      continue;
    }

    // Rename was successful; close <old-pid>.lock and then delete it
    pid_lockfile->Close();
    const FilesystemResult delete_res = fsw.Delete(pid_lockfile_path.CStr());

    // If we failed to delete the lockfile, log a warning but return success, as we've
    // completed the move of the process-level directory and now have <new-pid>/
    if (delete_res != FilesystemResult::OK) {
      _logger.Warning(
          "Failed to delete old-process lockfile after directory rename during storage "
          "migration",
          {{"path", pid_lockfile_path.Get()},
           {"error", FilesystemResultStr(delete_res)}}
      );
    }

    // We now have a <new-pid>/ directory that contains the event data left behind by
    // <old-pid>/
    _logger.Status(
        "Migrated data from abandoned process via directory rename",
        {{"src_path", src_pid_subdir_path.Get()},
         {"dst_path", dst_pid_subdir_path.Get()}}
    );
    return true;
  }

  // We found no directory to migrate; <new-pid>/ did not previously exist and has not
  // been created via rename
  return false;
}

void SdkStorage::MigrateAbandonedEventsToProcessRoot(FilesystemWrapper& fsw) {
  // This function is called after path members are initialized
  DATADOG_ASSERT(!_pid_str.empty(), "_pid_str is empty on migration");
  DATADOG_ASSERT(!_instance_root.Get().empty(), "_instance_root is empty on migration");
  DATADOG_ASSERT(!_process_root.Get().empty(), "_process_root is empty on migration");

  // Obtain a list of all subdirectories in our top-level SDK instance directory
  std::vector<std::string> subdirs;
  FilesystemResult res = fsw.ListSubdirectories(_instance_root.CStr(), subdirs);
  if (res != FilesystemResult::OK) {
    _logger.Warning(
        "Failed to perform storage migration: unable to list subdirectories in "
        "instance-level storage directory",
        {{"path", _instance_root.Get()}, {"error", FilesystemResultStr(res)}}
    );
    return;
  }

  // Iterate over each process-level subdirectory, attempting to take ownership of the
  // directory (if abandoned) and migrate its events file-by-file into _process_root
  for (const std::string& subdir_name : subdirs) {
    // Ignore the directory for our own PID
    if (subdir_name == _pid_str) {
      continue;
    }

    // Only consider directories whose names are integer-only; silently ignore others
    if (!std::all_of(subdir_name.begin(), subdir_name.end(), [](unsigned char c) {
          return std::isdigit(c);
        })) {
      continue;
    }

    // Build the full path to this process-level directory and its lockfile
    StoragePath pid_subdir_path;    // <_instance_root>/<old-pid>/
    StoragePath pid_lockfile_path;  // <_instance_root>/<old-pid>.lock
    if (!BuildProcessDirectoryPaths(subdir_name, pid_subdir_path, pid_lockfile_path)) {
      return;
    }

    // Attempt to open <old-pid>.lock for write and acquire a lock on that file
    auto pid_lockfile = AcquireAbandonedProcessDirectory(fsw, pid_lockfile_path);
    if (!pid_lockfile.has_value()) {
      // No luck acquiring a lock; proceed to next subdirectory
      continue;
    }

    // Iterate through <old-pid>/, handling each <feature>/ subdir and each consent
    // directory therein, by moving all event batch files to the corresponding
    // directory within <new-pid>/. Then delete the remnants of <old-pid>/ entirely.
    if (!HandleProcessDirectoryMigration(fsw, pid_subdir_path)) {
      // Something went wrong while attempting to migrate files from this directory:
      // leave everything as it is (close <old-pid>.lock but don't delete it) and move
      // on to the next subdirectory
      continue;
    }

    // Success: we've moved all events from <old-pid>/ to <new-pid>/, and <old-pid>/
    // no longer exists

    // File-by-file migration successful: close <old-pid>.lock and then delete it before
    // moving on to the next directory
    pid_lockfile->Close();
    const FilesystemResult delete_res = fsw.Delete(pid_lockfile_path.CStr());

    // If we failed to delete the lockfile, log a warning but continue regardless, as
    // we've completed migrating <old-pid>/
    if (delete_res != FilesystemResult::OK) {
      _logger.Warning(
          "Failed to delete old-process lockfile after file-by-file storage migration",
          {{"path", pid_lockfile_path.Get()},
           {"error", FilesystemResultStr(delete_res)}}
      );
    }

    // Continue to next loop iteration until we've checked all process-level directories
  }
}

bool SdkStorage::BuildProcessDirectoryPaths(
    std::string_view pid_subdir_name,
    StoragePath& pid_subdir_path,
    StoragePath& pid_lockfile_path
) {
  // Building paths requires us to respect storage path length limit
  const char* join_message =
      "Unable to perform storage migration: path exceeds length limit";

  // Join _instance_root + subdir_name to get the path to the directory containing
  // this foreign SDK instance's event data (organized into per-feature subdirs)
  if (!JoinPaths(
          pid_subdir_path, _instance_root.Get(), pid_subdir_name, _logger, join_message
      )) {
    // If appending one PID subdir gets us over the length limit, it's likely that
    // they all will: the root issue here is that the application-configured storage
    // path is too long relative to MAX_STORAGE_PATH_SIZE, so early-out
    return false;
  }

  // Append '.lock' to that path to get the path to the lockfile indicating whether
  // that data is still owned by an active process
  if (!pid_lockfile_path.Set(pid_subdir_path.Get())) {
    // We're storing a value that's already held in a StoragePath buffer; this can
    // never exceed the length limit
    DATADOG_ASSERT(
        false, "StoragePath::Set() failed to store value from StoragePath::Get()"
    );
    return false;
  }
  if (!AppendExtensionToPath(pid_lockfile_path, ".lock", _logger, join_message)) {
    // If appending ".lock" bumped us over the length limit, again, our root storage
    // path is too long and we should give up
    return false;
  }

  // We've populated pid_subdir_path with <instance_root>/<pid_subdir_name> and
  // pid_lockfile_path with <instance_root>/<pid_subdir_name>.lock
  return true;
}

std::optional<File> SdkStorage::AcquireAbandonedProcessDirectory(
    FilesystemWrapper& fsw, const StoragePath& pid_lockfile_path
) {
  // Attempt to open <old-pid>.lock, making a non-blocking attempt to hold an advisory
  // lock on that file. We open in write mode so that in the case where <old-pid>/
  // exists but <old-pid>.lock has been deleted or was never created, we'll implicitly
  // create it, making our claim to <old-pid>/ known regardless.
  const bool append = false;  // Lockfile has no contents; clobbering is fine
  const bool hold_advisory_lock = true;
  auto lockfile_res =
      fsw.OpenForWrite(pid_lockfile_path.CStr(), append, hold_advisory_lock);

  // If we were unable to open the lockfile, handle the error and move on to the next
  // subdirectory
  if (lockfile_res.value != FilesystemResult::OK) {
    // If the lockfile exists and we failed to open it because another process is
    // currently holding a lock on it, this is normal and expected: we should just
    // move on to the next subdirectory
    if (lockfile_res.value == FilesystemResult::LockContention) {
      _logger.Debug(
          "Skipping migration of process-level event directory: another process holds "
          "the lockfile",
          {{"path", pid_lockfile_path.Get()},
           {"error", FilesystemResultStr(lockfile_res.value)}}
      );
      return std::nullopt;
    }

    // Any other filesystem error is unexpected: we should log a warning, but still
    // continue
    _logger.Warning(
        "Ignoring process-level directory for storage migration: unable to open "
        "lockfile for write",
        {{"path", pid_lockfile_path.Get()},
         {"error", FilesystemResultStr(lockfile_res.value)}}
    );
    return std::nullopt;
  }

  // We now have an open handle to <old-pid>.lock, with an advisory lock (move-construct
  // into a temporary std::optional so we can return with NRVO)
  std::optional<File> result;
  result.emplace(std::move(lockfile_res.file));
  return result;
}

bool SdkStorage::HandleProcessDirectoryMigration(
    FilesystemWrapper& fsw, const StoragePath& pid_subdir_path
) {
  // List all feature-specific subdirectories within <old-pid>/
  std::vector<std::string> feature_subdir_names;
  const FilesystemResult res =
      fsw.ListSubdirectories(pid_subdir_path.CStr(), feature_subdir_names);
  if (res != FilesystemResult::OK) {
    _logger.Warning(
        "Failed to handle process-level storage directory migration: unable to list "
        "subdirectories",
        {{"path", pid_subdir_path.Get()}, {"error", FilesystemResultStr(res)}}
    );
    return false;
  }

  // Iterate over each feature subdirectory, ensuring that a corresponding directory
  // exists within _process_root and migrating all batches file-by-file from each
  // consent-level subdirectory
  for (const std::string& feature_name : feature_subdir_names) {
    if (!HandleFeatureDirectoryMigration(fsw, pid_subdir_path, feature_name)) {
      return false;
    }
  }

  _logger.Status(
      "Claimed all event data from abandoned process",
      {{"src_path", pid_subdir_path.Get()}, {"dst_path", _process_root.Get()}}
  );

  // Delete any remaining loose files (e.g. .DS_Store etc.) in the process directory,
  // reusing our vector of strings now that we're finished iterating over it
  std::vector<std::string>& loose_filenames = feature_subdir_names;
  if (!DeleteLooseFilesInDirectory(fsw, pid_subdir_path, loose_filenames)) {
    return false;
  }

  // Delete <old-pid>/ itself
  const char* rmdir_message =
      "Failed to handle process-level storage directory migration: unable to delete "
      "directory after moving files";
  if (!DeleteEmptyDirectory(pid_subdir_path, fsw, _logger, rmdir_message)) {
    return false;
  }

  // Success: all event data has been migrated to <instance>/<new-pid>/, and
  // <instance>/<old-pid>/ no longer exists
  return true;
}

bool SdkStorage::HandleFeatureDirectoryMigration(
    FilesystemWrapper& fsw,
    const StoragePath& pid_subdir_path,
    std::string_view feature_name
) {
  // Build a path to the source feature directory, as well as to the corresponding
  // feature directory within our current _process_root, i.e.
  // <instance>/<new-pid>/<feature>
  const char* join_message =
      "Failed to handle feature-level storage directory migration: path exceeds length "
      "limit";
  StoragePath src_path;
  src_path.MustSet(pid_subdir_path);
  if (!AppendPath(src_path, feature_name, _logger, join_message)) {
    return false;
  }
  StoragePath dst_path;
  if (!JoinPaths(dst_path, _process_root.Get(), feature_name, _logger, join_message)) {
    return false;
  }

  // Create that directory if it doesn't yet exist
  const char* mkdir_message =
      "Failed to handle feature-level storage directory migration: unable to create "
      "destination directory";
  if (!EnsureDirectoryExists(dst_path, fsw, _logger, mkdir_message)) {
    return false;
  }

  // List each subdirectory of the feature directory at src_path (e.g. 'v1/',
  // 'intermediate-v1/', etc.). This migration process is just a dumb recursive copy
  // with some assumptions about subdirectory/file depth; we don't care what these
  // directories are actually named
  std::vector<std::string> consent_subdir_names;
  const FilesystemResult res =
      fsw.ListSubdirectories(src_path.CStr(), consent_subdir_names);
  if (res != FilesystemResult::OK) {
    _logger.Warning(
        "Failed to handle feature-level storage directory migration: unable to list "
        "subdirectories",
        {{"path", src_path.Get()}, {"error", FilesystemResultStr(res)}}
    );
    return false;
  }

  // Iterate over each consent subdirectory, ensuring that a corresponding directory
  // exists within dst_path, and moving each batch file from src to dst
  for (const std::string& subdir_name : consent_subdir_names) {
    // Join <instance>/<old-pid>/<feature> + <consent>
    StoragePath src_subdir_path;
    src_subdir_path.MustSet(src_path);
    if (!AppendPath(src_subdir_path, subdir_name, _logger, join_message)) {
      return false;
    }

    // Join <instance>/<new-pid>/<feature> + <consent>
    StoragePath dst_subdir_path;
    dst_subdir_path.MustSet(dst_path);
    if (!AppendPath(dst_subdir_path, subdir_name, _logger, join_message)) {
      return false;
    }

    // Move all files from src_subdir_path to dst_subdir_path, then delete the source
    // directory
    if (!HandleConsentDirectoryMigration(fsw, src_subdir_path, dst_subdir_path)) {
      return false;
    }
  }

  // Delete any remaining loose files (e.g. .DS_Store etc.) in the feature directory,
  // reusing our vector of strings now that we're finished iterating over it
  std::vector<std::string>& loose_filenames = consent_subdir_names;
  if (!DeleteLooseFilesInDirectory(fsw, src_path, loose_filenames)) {
    return false;
  }

  // Delete <old-pid>/feature/ itself
  const char* rmdir_message =
      "Failed to handle feature-level storage directory migration: unable to delete "
      "directory after moving files";
  if (!DeleteEmptyDirectory(src_path, fsw, _logger, rmdir_message)) {
    return false;
  }

  // Success, all directories within <old-pid>/<feature>/... have been moved to
  // <new-pid>/feature/..., and <old-pid>/<feature>/ is now empty
  return true;
}

bool SdkStorage::HandleConsentDirectoryMigration(
    FilesystemWrapper& fsw,
    const StoragePath& src_dir_path,
    const StoragePath& dst_dir_path
) {
  // Create a directory at dst_dir_path if none exists yet
  const char* mkdir_message =
      "Failed to handle consent-level storage directory migration: unable to create "
      "directory";
  if (!EnsureDirectoryExists(dst_dir_path, fsw, _logger, mkdir_message)) {
    return false;
  }

  // Get a list of all regular files within src_dir_path
  std::vector<std::string> filenames;
  const FilesystemResult list_res = fsw.ListFiles(src_dir_path.CStr(), filenames);
  if (list_res != FilesystemResult::OK) {
    _logger.Warning(
        "Failed to handle consent-level storage directory migration: unable to list "
        "files",
        {{"path", src_dir_path.Get()}, {"error", FilesystemResultStr(list_res)}}
    );
    return false;
  }

  const char* join_message =
      "Failed to handle consent-level storage directory migration: path exceeds length "
      "limit";

  // Iterate over all files in src_dir, moving each one to dst_dir
  for (const std::string& filename : filenames) {
    // Append <instance>/<old-pid>/<feature>/<consent> + <filename>
    StoragePath src_file_path;
    src_file_path.MustSet(src_dir_path);
    if (!AppendPath(src_file_path, filename, _logger, join_message)) {
      return false;
    }

    // Append <instance>/<new-pid>/<feature>/<consent> + <filename>
    StoragePath dst_file_path;
    dst_file_path.MustSet(dst_dir_path);
    if (!AppendPath(dst_file_path, filename, _logger, join_message)) {
      return false;
    }

    // n.b. the SDK only writes batch files (containing event data) with numeric
    // filenames, whereas the OS or the user might have added other files (e.g.
    // .DS_Store) - we don't really care about differentiating between those files here;
    // we just move everything wholesale

    // Attempt to rename <src_dir>/<filename> -> <dst_dir>/<filename>
    const FilesystemResult rename_res =
        fsw.Rename(src_file_path.CStr(), dst_file_path.CStr());

    // IFilesystem::Rename() does not clobber existing destination files: if the rename
    // failed because there's already a file at dst_file_path, we have a name conflict
    if (rename_res == FilesystemResult::AlreadyExists) {
      // In that case, delete the source file and proceed
      const FilesystemResult delete_res = fsw.Delete(src_file_path.CStr());
      if (delete_res != FilesystemResult::OK) {
        // Delete failed: log a warning and abort
        _logger.Warning(
            "Failed to handle consent-level storage directory migration: unable to "
            "delete file",
            {{"path", src_file_path.Get()}, {"error", FilesystemResultStr(delete_res)}}
        );
        return false;
      }

      // Delete OK; move on to next file
      _logger.Warning(
          "On consent-level storage directory migration, resolved filename conflict by "
          "deleting source file",
          {{"src_path", src_file_path.Get()}, {"dst_path", dst_file_path.Get()}}
      );
      continue;
    }

    // If Rename() failed for any other reason, abort
    if (rename_res != FilesystemResult::OK) {
      _logger.Warning(
          "Failed to handle consent-level storage directory migration: unable to "
          "rename file",
          {{"src_path", src_file_path.Get()},
           {"dst_path", dst_file_path.Get()},
           {"error", FilesystemResultStr(rename_res)}}
      );
      return false;
    }

    // File renamed OK; continue to next file
  }

  // We've moved all files out of the source directory, so we should be able to delete
  // it now (unless the user has manually created more nested subdirectories, or it's a
  // Windows folder that's open in explorer or cmd.exe, etc.)
  const char* rmdir_message =
      "Failed to handle consent-level storage directory migration: unable to delete "
      "directory after moving files";
  if (!DeleteEmptyDirectory(src_dir_path, fsw, _logger, rmdir_message)) {
    return false;
  }

  // Success: all files renamed or deleted without incident, and source directory no
  // longer exists
  return true;
}

bool SdkStorage::DeleteLooseFilesInDirectory(
    FilesystemWrapper& fsw,
    const StoragePath& dir_path,
    std::vector<std::string>& out_filenames
) {
  // List all regular files in the given directory, reusing the provided vector of
  // strings
  const FilesystemResult list_res = fsw.ListFiles(dir_path.CStr(), out_filenames);
  if (list_res != FilesystemResult::OK) {
    _logger.Warning(
        "Failed to handle storage directory migration: unable to list files",
        {{"path", dir_path.Get()}, {"error", FilesystemResultStr(list_res)}}
    );
    return false;
  }

  // Attempt to delete each file, aborting on failure
  for (const std::string& filename : out_filenames) {
    // Append filename to get full path to file
    StoragePath file_path;
    file_path.MustSet(dir_path);
    const char* join_message =
        "Failed to handle storage directory migration: path exceeds length limit";
    if (!AppendPath(file_path, filename, _logger, join_message)) {
      return false;
    }

    // Delete the target file
    const FilesystemResult delete_res = fsw.Delete(file_path.CStr());

    // If unable to delete, log a warning and abort
    if (delete_res != FilesystemResult::OK) {
      _logger.Warning(
          "Failed to handle storage directory migration: unable to delete loose file",
          {{"path", file_path.Get()}, {"error", FilesystemResultStr(delete_res)}}
      );
      return false;
    }

    // Delete OK: log a status message and proceed to the next file
    _logger.Status(
        "Deleted loose file on storage directory migration", {{"path", file_path.Get()}}
    );
  }

  // Success: all regular files in this directory have been deleted, if any existed
  return true;
}

}  // namespace datadog::impl
