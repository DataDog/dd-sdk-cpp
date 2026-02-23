// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <charconv>

#include "datadog/impl/diagnostics.hpp"
#include "datadog/impl/storage/filesystem.hpp"
#include "datadog/impl/storage/path.hpp"

namespace datadog::impl {

static const char* filesystem_result_str(FilesystemResult res) {
  switch (res) {
    case FilesystemResult::OK:
      return "OK";
    case FilesystemResult::AlreadyExistsAsDirectory:
      return "AlreadyExistsAsDirectory";
    case FilesystemResult::AlreadyExists:
      return "AlreadyExists";
    case FilesystemResult::DoesNotExist:
      return "DoesNotExist";
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
    case FilesystemResult::LockContention:
      return "LockContention";
    case FilesystemResult::UnknownError:
      return "UnknownError";
  }
  return "<invalid-enum>";
}

void LogPathLengthError(
    const class DiagnosticLogger& logger, const char* failure_message
) {
  (void)logger;
  (void)failure_message;
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
    PlatformPath& platform_path,
    IFilesystem& fs,
    const DiagnosticLogger& logger,
    const char* failure_message
) {
  // Convert from the SDK's canonical UTF-8 path representation to the platform's
  // required string encoding for paths: on Windows this converts to UTF-16 and should
  // always succeed so long as the input path is valid UTF-8; on other platforms this
  // does nothing and will never fail
  if (!platform_path.Encode(path.CStr())) {
    logger.Error(failure_message, {{"path", path.CStr()}, {"operation", "encode"}});
    return false;
  }

  // Use platform filesystem APIs to create the desired directory, and/or detect whether
  // it's a directory if it already exists
  auto res = fs.CreateDirectory(platform_path);
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
       {"error", filesystem_result_str(res)}}
  );
  return false;
}

}  // namespace datadog::impl
