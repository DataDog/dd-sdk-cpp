// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "datadog/impl/storage/filesystem.hpp"

namespace datadog::impl {

class WindowsFilesystem final : public IFilesystem {
 public:
  FilesystemResult CreateDirectory(const PlatformPath& path) override {
    // Attempt to create directory with default security attributes
    const BOOL result = CreateDirectoryW(path.Get(), nullptr);
    if (result != 0) {
      return FilesystemResult::OK;
    }

    // Creation failed, analyze GetLastError() to determine specific failure reason
    const DWORD error = GetLastError();
    switch (error) {
      case ERROR_ALREADY_EXISTS: {
        // Path exists - use GetFileAttributesW() to distinguish file vs directory
        const DWORD attrs = GetFileAttributesW(path.Get());
        if (attrs != INVALID_FILE_ATTRIBUTES) {
          if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
            return FilesystemResult::AlreadyExistsAsDirectory;
          }
          return FilesystemResult::AlreadyExistsAsFile;
        }
        // GetFileAttributesW() failed - path exists but we can't determine type
        return FilesystemResult::UnknownError;
      }
      case ERROR_PATH_NOT_FOUND:
        return FilesystemResult::ParentDirectoryDoesNotExist;
      case ERROR_ACCESS_DENIED:
        return FilesystemResult::PermissionDenied;
      case ERROR_WRITE_PROTECT:
        return FilesystemResult::ReadOnlyFilesystem;
      case ERROR_DISK_FULL:
      case ERROR_HANDLE_DISK_FULL:
        return FilesystemResult::OutOfSpace;
      case ERROR_FILENAME_EXCED_RANGE:
      case ERROR_BAD_PATHNAME:
        // Path too long or invalid path format
        return FilesystemResult::PathTooLong;
      case ERROR_INVALID_NAME:
      case ERROR_BAD_NET_NAME:
      case ERROR_DIRECTORY:
        // Invalid name or path component is a file
        return FilesystemResult::InvalidName;
      default:
        // Unexpected error
        return FilesystemResult::UnknownError;
    }
  }
};

}  // namespace datadog::impl
