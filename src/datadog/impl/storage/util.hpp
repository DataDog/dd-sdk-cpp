// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <string_view>

#include "datadog/impl/storage/filesystem.hpp"

namespace datadog::impl {

/**
 * Returns the string literal representation of the given FilesystemResult enum value.
 */
const char* FilesystemResultStr(FilesystemResult result);

/**
 * Wraps a call to `dst.Append(name)`, logging a descriptive error message in the event
 * that the resulting path exceeds the maxmimum supported path length.
 */
bool AppendPath(
    class StoragePath& dst,
    std::string_view name,
    const class DiagnosticLogger& logger,
    const char* failure_message
);

/**
 * Wraps a call to `dst.AppendExt(ext)`, logging a descriptive error message in the
 * event that the resulting path exceeds the maximum supported path length.
 */
bool AppendExtensionToPath(
    class StoragePath& dst,
    std::string_view ext,
    const class DiagnosticLogger& logger,
    const char* failure_message
);

/**
 * Populates `dst` with the result of appending `parent` + `name`, logging a descriptive
 * error message if either the `dst.Set(parent)` or `dst.Append(name)` operation fails
 * due to the path length limit.
 */
bool JoinPaths(
    class StoragePath& dst,
    std::string_view parent,
    std::string_view name,
    const class DiagnosticLogger& logger,
    const char* failure_message
);

/**
 * Encodes path into the provided PlatformPath buffer, then wraps a call to
 * `fs.CreateDirectory` using that path, logging a descriptive error message if either
 * path encoding or the CreateDirectory call fail. Returns true if the desired directory
 * exists, either because it was successfully created or because there was already a
 * directory at the given path.
 */
bool EnsureDirectoryExists(
    const class StoragePath& path,
    class PlatformPath& platform_path,
    class IFilesystem& fs,
    const class DiagnosticLogger& logger,
    const char* failure_message
);

/**
 * Encodes path into the provided PlatformPath buffer, then wraps a call to
 * `fs.DeleteDirectory` using that path, logging a descriptived error message if either
 * path encoding or the DeleteDirectory call fail. Returns true if the directory was
 * successfully deleted.
 */
bool DeleteEmptyDirectory(
    const class StoragePath& path,
    class PlatformPath& platform_path,
    class IFilesystem& fs,
    const class DiagnosticLogger& logger,
    const char* failure_message
);

/**
 * RAII wrapper used to ensure that file handles are closed when no longer in scope.
 */
struct FileHandleWrapper {
  class IFilesystem& fs;
  PlatformFileHandle handle;

  explicit FileHandleWrapper(class IFilesystem& in_fs, PlatformFileHandle in_handle)
      : fs(in_fs), handle(in_handle) {}

  ~FileHandleWrapper() {
    if (handle != INVALID_FILE_HANDLE) {
      fs.Close(handle);
    }
  }

  FileHandleWrapper(const FileHandleWrapper&) = delete;
  FileHandleWrapper& operator=(const FileHandleWrapper&) = delete;
  FileHandleWrapper(FileHandleWrapper&&) = delete;
  FileHandleWrapper& operator=(FileHandleWrapper&&) = delete;
};

}  // namespace datadog::impl
