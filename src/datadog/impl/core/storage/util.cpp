// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/storage/util.hpp"

#include "datadog/impl/core/storage/path.hpp"
#include "datadog/impl/core/util/diagnostics.hpp"

namespace datadog::impl {

const char* FilesystemResultStr(FilesystemResult res) {
  switch (res) {
    case FilesystemResult::OK:
      return "OK";
    case FilesystemResult::AlreadyExistsAsDirectory:
      return "AlreadyExistsAsDirectory";
    case FilesystemResult::AlreadyExists:
      return "AlreadyExists";
    case FilesystemResult::DoesNotExist:
      return "DoesNotExist";
    case FilesystemResult::DirectoryNotEmpty:
      return "DirectoryNotEmpty";
    case FilesystemResult::PermissionDenied:
      return "PermissionDenied";
    case FilesystemResult::ReadOnlyFilesystem:
      return "ReadOnlyFilesystem";
    case FilesystemResult::OutOfSpace:
      return "OutOfSpace";
    case FilesystemResult::PathTooLong:
      return "PathTooLong";
    case FilesystemResult::InvalidName:
      return "InvalidName";
    case FilesystemResult::PathEncodingFailed:
      return "PathEncodingFailed";
    case FilesystemResult::LockContention:
      return "LockContention";
    case FilesystemResult::UnknownError:
      return "UnknownError";
  }
  return "<invalid-enum>";
}

bool AppendPath(
    StoragePath& dst,
    std::string_view name,
    const DiagnosticLogger& logger,
    const char* failure_message
) {
  if (!dst.Append(name)) {
    logger.Error(
        failure_message,
        {{"parent_path", dst.Get()},
         {"name", name},
         {"max_storage_path_size", MAX_STORAGE_PATH_SIZE}}
    );
    return false;
  }
  return true;
}

bool AppendExtensionToPath(
    class StoragePath& dst,
    std::string_view ext,
    const class DiagnosticLogger& logger,
    const char* failure_message
) {
  if (!dst.AppendExt(ext)) {
    logger.Error(
        failure_message,
        {{"path", dst.Get()},
         {"ext", ext},
         {"max_storage_path_size", MAX_STORAGE_PATH_SIZE}}
    );
    return false;
  }
  return true;
}

bool JoinPaths(
    StoragePath& dst,
    std::string_view parent,
    std::string_view name,
    const DiagnosticLogger& logger,
    const char* failure_message
) {
  if (!dst.Set(parent)) {
    logger.Error(
        failure_message,
        {{"parent_path", parent},
         {"name", name},
         {"max_storage_path_size", MAX_STORAGE_PATH_SIZE}}
    );
    return false;
  }
  return AppendPath(dst, name, logger, failure_message);
}

bool EnsureDirectoryExists(
    const StoragePath& path,
    FilesystemWrapper& fsw,
    const DiagnosticLogger& logger,
    const char* failure_message
) {
  // Use platform filesystem APIs to create the desired directory, and/or detect whether
  // it's a directory if it already exists
  const FilesystemResult res = fsw.CreateDirectory(path.CStr());
  if (res == FilesystemResult::OK ||
      res == FilesystemResult::AlreadyExistsAsDirectory) {
    // There is now a valid directory at `path`; mission accomplished
    return true;
  }

  // If the directory didn't already exist and we couldn't create it, log an error and
  // fail
  logger.Error(
      failure_message,
      {{"path", path.CStr()},
       {"operation", "create"},
       {"error", FilesystemResultStr(res)}}
  );
  return false;
}

bool DeleteEmptyDirectory(
    const class StoragePath& path,
    FilesystemWrapper& fsw,
    const class DiagnosticLogger& logger,
    const char* failure_message
) {
  // Attempt to delete the directory: this uses rmdir/RemoveDirectoryW, requiring that
  // the directory be empty
  const FilesystemResult res = fsw.DeleteDirectory(path.CStr());
  if (res != FilesystemResult::OK) {
    // Log a warning message that includes the result enum and report failure
    logger.Warning(
        failure_message,
        {{"path", path.CStr()},
         {"operation", "create"},
         {"error", FilesystemResultStr(res)}}
    );
    return false;
  }

  // Success: directory has been deleted
  return true;
}

}  // namespace datadog::impl
