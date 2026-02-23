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
  if (!lockfile_path.Set(_root.Get()) ||
      !lockfile_path.Append(abandoned_pid) ||
      !lockfile_path.Append(".lock")) {
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
  if (!src_platform.Encode(src_path.CStr()) ||
      !dst_platform.Encode(dst_path.CStr())) {
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

  // Build lockfile path: <root>/<pid>.lock (sibling to process directory)
  StoragePath lockfile_path;
  if (!JoinPaths(lockfile_path, _root.Get(), _pid_str, logger, join_message)) {
    return false;
  }
  if (!AppendPath(lockfile_path, ".lock", logger, join_message)) {
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

  // Create process directory
  if (!EnsureDirectoryExists(_process_root, path, _fs, logger, mkdir_message)) {
    return false;
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
    if (!lockfile_path.Set(_root.Get()) || !lockfile_path.Append(name)) {
      continue;
    }
    if (!lockfile_path.Append("lockfile")) {
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

void SdkStorage::HandleMigrate(std::string_view from_pid) {  // NOLINT
  (void)from_pid;
  /*
  StoragePath from_process_root;
  StoragePath instance_root;
  PlatformPath path;

  std::vector<std::string> instance_names;
  instance_names.reserve(4);
  std::vector<std::string> feature_names;
  feature_names.reserve(8);
  std::vector<std::string> subdir_names;
  subdir_names.reserve(4);

  if (!from_process_root.Join(_events_root.Get(), from_pid)) {
    return;
  }

  if (!path.Encode(from_process_root.CStr())) {
    return;
  }
  if (_fs.ListSubdirectories(path, instance_names) != FilesystemResult::OK) {
    return;
  }

  for (const std::string& instance_name : instance_names) {
    if (!instance_root.Join(from_process_root.Get(), instance_name)) {
      continue;
    }
    if (!path.Encode(instance_root.Get())) {
    }
  }
  */
}

}  // namespace datadog::impl
