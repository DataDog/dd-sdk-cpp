// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <errno.h>
#include <sys/stat.h>

#include "datadog/impl/storage/filesystem.hpp"

namespace datadog::impl {

class PosixFilesystem final : public IFilesystem {
 public:
  FilesystemResult CreateDirectory(const PlatformPath& path) override {
    // Attempt to create directory with rwx------ permissions
    const int result = mkdir(path.Get(), 0700);
    if (result == 0) {
      return FilesystemResult::OK;
    }

    // Creation failed, analyze errno to determine specific failure reason
    const int error = errno;
    switch (error) {
      case EEXIST: {
        // Path exists - use stat() to distinguish file vs directory
        struct stat st;  // NOLINT(cppcoreguidelines-pro-type-member-init)
        if (stat(path.Get(), &st) == 0) {
          if (S_ISDIR(st.st_mode)) {
            return FilesystemResult::AlreadyExistsAsDirectory;
          }
          return FilesystemResult::AlreadyExistsAsFile;
        }
        // stat() failed - path exists but we can't determine type
        return FilesystemResult::UnknownError;
      }
      case ENOENT:
        return FilesystemResult::ParentDirectoryDoesNotExist;
      case EACCES:
      case EPERM:
        return FilesystemResult::PermissionDenied;
      case EROFS:
        return FilesystemResult::ReadOnlyFilesystem;
      case ENOSPC:
      case EDQUOT:
        // Out of space or quota exceeded
        return FilesystemResult::OutOfSpace;
      case ENAMETOOLONG:
        return FilesystemResult::PathTooLong;
      case EINVAL:
      case ELOOP:
      case ENOTDIR:
        // Invalid name, too many symlinks, or path component is not a directory
        return FilesystemResult::InvalidName;
      default:
        // Unexpected error
        return FilesystemResult::UnknownError;
    }
  }
};

}  // namespace datadog::impl
