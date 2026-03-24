// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/storage/sdk.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>

#include "datadog/impl/assert.hpp"
#include "datadog/impl/storage/util.hpp"

namespace datadog::impl {

SdkStorage::SdkStorage(IFilesystem& fs, uint32_t pid) : _fs(fs), _pid(pid) {}

SdkStorage::~SdkStorage() {
  if (_lockfile_handle != INVALID_FILE_HANDLE) {
    _fs.Close(_lockfile_handle);
  }
}

bool SdkStorage::TryClaimAbandonedDirectory(std::string_view abandoned_pid) {
  // Build lockfile path: <root>/<abandoned_pid>.lock
  StoragePath lockfile_path;
  if (!lockfile_path.Set(_root.Get()) || !lockfile_path.Append(abandoned_pid) ||
      !lockfile_path.AppendExt(".lock")) {
    return false;
  }

  // Try to acquire lock
  PlatformPath path;
  if (!path.Encode(lockfile_path.CStr())) {
    return false;
  }

  auto opened = _fs.OpenForRead(path, true);

  // Lock contention = process still alive
  if (opened.value == FilesystemResult::LockContention) {
    return false;
  }

  // File doesn't exist = no lockfile (old SDK version or cleaned up)
  if (opened.value == FilesystemResult::DoesNotExist) {
    return false;
  }

  // Other errors = skip
  if (opened.value != FilesystemResult::OK || opened.handle == INVALID_FILE_HANDLE) {
    return false;
  }

  // Successfully locked = process dead, directory abandoned
  // Build paths for rename: <root>/<abandoned_pid>/ → <root>/<current_pid>/
  StoragePath src_path;
  StoragePath dst_path;
  if (!src_path.Set(_root.Get()) || !src_path.Append(abandoned_pid)) {
    _fs.Close(opened.handle);
    return false;
  }
  if (!dst_path.Set(_root.Get()) || !dst_path.Append(_pid_str)) {
    _fs.Close(opened.handle);
    return false;
  }

  PlatformPath src_platform;
  PlatformPath dst_platform;
  if (!src_platform.Encode(src_path.CStr()) || !dst_platform.Encode(dst_path.CStr())) {
    _fs.Close(opened.handle);
    return false;
  }

  // Atomic rename
  FilesystemResult rename_result = _fs.Rename(src_platform, dst_platform);

  _fs.Close(opened.handle);

  if (rename_result == FilesystemResult::OK) {
    // Delete old lockfile
    _fs.Delete(path);
    return true;
  }

  return false;
}

bool SdkStorage::Initialize(
    const impl::DiagnosticLogger& logger,
    std::string_view application_storage_path,
    std::string_view sdk_instance_name
) {
  // Use a temporary buffer to build UTF-8 paths, and a
  // Use a buffer to convert to platform-native paths for filesystem operations
  PlatformPath path;

  // Prepare error messages to be logged if any of other path-manipulation or filesystem
  // helper fuctions fails
  const char* join_message =
      "Failed to initialize SDK storage from configured application storage path: path "
      "exceeds length limit";
  const char* mkdir_message =
      "Failed to initialize SDK storage from configured application storage path: "
      "unable to create directory";
  const char* lockfile_message =
      "Failed to initialize SDK storage: unable to acquire lockfile";

  // Convert our PID to string for easy comparison and path-building
  auto res = std::to_chars(
      _pid_str_buffer.data(), _pid_str_buffer.data() + _pid_str_buffer.size() - 1, _pid
  );
  DATADOG_ASSERT(res.ec == std::errc{}, "Failed to convert uint32_t PID to string");
  *res.ptr = '\0';
  _pid_str = std::string_view{_pid_str_buffer.data()};

  // Root SDK storage directory is <application-storage>/.datadog: this is the only
  // directory where the SDK will read or write files
  if (!JoinPaths(_root, application_storage_path, ".datadog", logger, join_message)) {
    return false;
  }
  if (!EnsureDirectoryExists(_root, path, _fs, logger, mkdir_message)) {
    return false;
  }

  // Scan for abandoned process directories to migrate
  bool claimed_abandoned = false;
  {
    PlatformPath scan_path;
    if (scan_path.Encode(_root.CStr())) {
      std::vector<std::string> subdirs;
      if (_fs.ListSubdirectories(scan_path, subdirs) == FilesystemResult::OK) {
        for (const std::string& name : subdirs) {
          // Skip our own PID
          if (name == _pid_str) {
            continue;
          }

          // Only try numeric directory names (PIDs)
          if (!std::all_of(name.begin(), name.end(), ::isdigit)) {
            continue;
          }

          // Try to claim this abandoned directory
          if (TryClaimAbandonedDirectory(name)) {
            claimed_abandoned = true;
            break;  // Successfully claimed, stop searching
          }
        }
      }
    }
  }

  // Build lockfile path: <root>/<pid>.lock (sibling to process directory)
  StoragePath lockfile_path;
  if (!JoinPaths(lockfile_path, _root.Get(), _pid_str, logger, join_message) ||
      !lockfile_path.AppendExt(".lock")) {
    return false;
  }

  // Encode and acquire lockfile BEFORE creating process directory
  if (!path.Encode(lockfile_path.CStr())) {
    logger.Error("Failed to encode lockfile path");
    return false;
  }

  const bool append = false;
  const bool hold_advisory_lock = true;
  auto opened = _fs.OpenForWrite(path, append, hold_advisory_lock);
  if (opened.value != FilesystemResult::OK || opened.handle == INVALID_FILE_HANDLE) {
    logger.Error(lockfile_message);
    return false;
  }
  _lockfile_handle = opened.handle;

  // Build process directory path: <application-storage>/.datadog/<pid> will
  // contain event data for this process, ensuring that we don't contend with other
  // processes of the same application that may be running concurrently
  if (!JoinPaths(_process_root, _root.Get(), _pid_str, logger, join_message)) {
    return false;
  }

  // Create process directory (unless we claimed an abandoned one via rename)
  if (!claimed_abandoned) {
    if (!EnsureDirectoryExists(_process_root, path, _fs, logger, mkdir_message)) {
      return false;
    }
  }

  // If we have multiple SDK instances within the same process, they must be configured
  // with unique "instance names" (default is "main"): we use another layer of nesting
  // to establish <application-storage>/.datadog/<pid>/<instance-name>: this
  // `_events_root` directory is where feature-specific subdirectories will be created
  if (!JoinPaths(
          _events_root, _process_root.Get(), sdk_instance_name, logger, join_message
      )) {
    return false;
  }
  if (!EnsureDirectoryExists(_events_root, path, _fs, logger, mkdir_message)) {
    return false;
  }

  // Migrate events from any remaining abandoned directories that couldn't be claimed
  // via directory rename. This handles the case where multiple processes crashed and
  // we've already claimed one directory - we need to migrate events from the others.
  MigrateAbandonedEvents();

  return true;
}

void SdkStorage::MigrateAbandonedEvents() {
  PlatformPath path;
  if (!path.Encode(_root.CStr())) {
    return;
  }

  std::vector<std::string> names;
  if (_fs.ListSubdirectories(path, names) != FilesystemResult::OK) {
    return;
  }

  StoragePath lockfile_path;
  for (const std::string& name : names) {
    if (name == _pid_str) {
      continue;
    }
    if (!std::all_of(name.begin(), name.end(), isdigit)) {
      continue;
    }

    // Build lockfile path: <root>/<name>.lock
    if (!lockfile_path.Set(_root.Get()) || !lockfile_path.Append(name) ||
        !lockfile_path.AppendExt(".lock")) {
      continue;
    }

    if (!path.Encode(lockfile_path.CStr())) {
      continue;
    }
    auto res = _fs.OpenForRead(path, true);
    if (res.value == FilesystemResult::LockContention) {
      continue;
    }
    if (res.value != FilesystemResult::OK || res.handle == INVALID_FILE_HANDLE) {
      continue;
    }

    HandleMigrate(name);

    _fs.Close(res.handle);
    _fs.Delete(path);
  }
}

void SdkStorage::MigrateFilesFromSubdirectory(
    const StoragePath& from_events_dir, const StoragePath& to_events_dir
) {
  PlatformPath path;
  PlatformPath src_path;
  PlatformPath dst_path;

  if (!path.Encode(from_events_dir.CStr())) {
    return;
  }

  std::vector<std::string> filenames;
  if (_fs.ListFiles(path, filenames) != FilesystemResult::OK) {
    return;
  }

  for (const std::string& filename : filenames) {
    StoragePath src_file;
    if (!src_file.Set(from_events_dir.Get()) || !src_file.Append(filename)) {
      continue;
    }

    StoragePath dst_file;
    if (!dst_file.Set(to_events_dir.Get()) || !dst_file.Append(filename)) {
      continue;
    }

    if (!src_path.Encode(src_file.CStr()) || !dst_path.Encode(dst_file.CStr())) {
      continue;
    }

    _fs.Rename(src_path, dst_path);
  }
}

bool SdkStorage::EnsureDestinationDirectoryExists(
    std::string_view instance_name,
    std::string_view feature_name,
    std::string_view subdir
) {
  PlatformPath path;

  // Create instance directory
  StoragePath instance_dir;
  if (!instance_dir.Set(_process_root.Get()) || !instance_dir.Append(instance_name)) {
    return false;
  }
  if (!path.Encode(instance_dir.CStr())) {
    return false;
  }
  auto res = _fs.CreateDirectory(path);
  if (res != FilesystemResult::OK &&
      res != FilesystemResult::AlreadyExistsAsDirectory) {
    return false;
  }

  // Create feature directory
  StoragePath feature_dir;
  if (!feature_dir.Set(instance_dir.Get()) || !feature_dir.Append(feature_name)) {
    return false;
  }
  if (!path.Encode(feature_dir.CStr())) {
    return false;
  }
  res = _fs.CreateDirectory(path);
  if (res != FilesystemResult::OK &&
      res != FilesystemResult::AlreadyExistsAsDirectory) {
    return false;
  }

  // Create events subdirectory (v1/ or intermediate-v1/)
  StoragePath events_dir;
  if (!events_dir.Set(feature_dir.Get()) || !events_dir.Append(subdir)) {
    return false;
  }
  if (!path.Encode(events_dir.CStr())) {
    return false;
  }
  res = _fs.CreateDirectory(path);
  if (res != FilesystemResult::OK &&
      res != FilesystemResult::AlreadyExistsAsDirectory) {
    return false;
  }

  return true;
}

void SdkStorage::MigrateFeatureEvents(
    std::string_view instance_name,
    std::string_view feature_name,
    const StoragePath& from_feature_root
) {
  const char* subdirs[] = {"v1", "intermediate-v1"};
  for (const char* subdir : subdirs) {
    StoragePath from_events_dir;
    if (!from_events_dir.Set(from_feature_root.Get()) ||
        !from_events_dir.Append(subdir)) {
      continue;
    }

    if (!EnsureDestinationDirectoryExists(instance_name, feature_name, subdir)) {
      continue;
    }

    StoragePath to_events_dir;
    if (!to_events_dir.Set(_process_root.Get()) ||
        !to_events_dir.Append(instance_name) || !to_events_dir.Append(feature_name) ||
        !to_events_dir.Append(subdir)) {
      continue;
    }

    MigrateFilesFromSubdirectory(from_events_dir, to_events_dir);
  }
}

void SdkStorage::MigrateInstanceDirectory(
    std::string_view instance_name, const StoragePath& from_instance_root
) {
  PlatformPath path;
  StoragePath from_feature_root;

  if (!path.Encode(from_instance_root.CStr())) {
    return;
  }

  std::vector<std::string> feature_names;
  if (_fs.ListSubdirectories(path, feature_names) != FilesystemResult::OK) {
    return;
  }

  for (const std::string& feature_name : feature_names) {
    if (!from_feature_root.Set(from_instance_root.Get()) ||
        !from_feature_root.Append(feature_name)) {
      continue;
    }

    MigrateFeatureEvents(instance_name, feature_name, from_feature_root);
  }
}

void SdkStorage::HandleMigrate(std::string_view from_pid) {
  StoragePath from_process_root;
  StoragePath from_instance_root;
  PlatformPath path;

  // Build source process root: <root>/<abandoned_pid>/
  if (!from_process_root.Set(_root.Get()) || !from_process_root.Append(from_pid)) {
    return;
  }

  // List all instance directories (e.g., "main")
  if (!path.Encode(from_process_root.CStr())) {
    return;
  }
  std::vector<std::string> instance_names;
  if (_fs.ListSubdirectories(path, instance_names) != FilesystemResult::OK) {
    return;
  }

  // For each instance directory
  for (const std::string& instance_name : instance_names) {
    if (!from_instance_root.Set(from_process_root.Get()) ||
        !from_instance_root.Append(instance_name)) {
      continue;
    }

    MigrateInstanceDirectory(instance_name, from_instance_root);
  }

  // Clean up: delete the abandoned process directory and its lockfile
  if (path.Encode(from_process_root.CStr())) {
    _fs.Delete(path);
  }

  // Build lockfile path: <root>/<from_pid>.lock
  StoragePath lockfile;
  if (lockfile.Set(_root.Get()) && lockfile.Append(from_pid) &&
      lockfile.AppendExt(".lock") && path.Encode(lockfile.CStr())) {
    _fs.Delete(path);
  }
}

}  // namespace datadog::impl
