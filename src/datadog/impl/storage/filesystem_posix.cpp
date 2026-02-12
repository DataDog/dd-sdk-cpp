// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>

#include "datadog/impl/storage/filesystem.hpp"

// NOLINTBEGIN(concurrency-mt-unsafe)
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
// NOLINTBEGIN(cppcoreguidelines-pro-type-member-init)
// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg)

namespace datadog::impl {

// === Error Mapping Helper ===
// Maps POSIX errno values to FilesystemResult codes. Used by most operations
// for consistent error reporting. CreateDirectory() handles errors inline since
// it needs to distinguish AlreadyExists vs AlreadyExistsAsDirectory.
namespace {
FilesystemResult MapErrnoToResult(int err) {
  switch (err) {
    case ENOENT:
      return FilesystemResult::ParentDirectoryDoesNotExist;
    case EEXIST:
      return FilesystemResult::AlreadyExists;
    case EACCES:
    case EPERM:
      return FilesystemResult::PermissionDenied;
    case EROFS:
      return FilesystemResult::ReadOnlyFilesystem;
    case ENOSPC:
    case EDQUOT:
      return FilesystemResult::OutOfSpace;
    case ENAMETOOLONG:
      return FilesystemResult::PathTooLong;
    case EINVAL:
    case ELOOP:
    case ENOTDIR:
      return FilesystemResult::InvalidName;
    case EWOULDBLOCK:
      return FilesystemResult::LockContention;
    default:
      return FilesystemResult::UnknownError;
  }
}
}  // namespace

class PosixFilesystem final : public IFilesystem {
 public:
  // === Directory Operations ===

  FilesystemResult CreateDirectory(const PlatformPath& path) override {
    // Attempt to create directory with rwx------ permissions
    const int result = mkdir(path.Get(), 0700);
    if (result == 0) {
      return FilesystemResult::OK;
    }

    // Creation failed, analyze errno to determine specific failure reason.
    // We handle errors inline here (rather than using MapErrnoToResult) because
    // EEXIST requires calling stat() to distinguish between an existing file vs
    // an existing directory.
    const int error = errno;
    if (error == EEXIST) {
      // Path exists - use stat() to distinguish file vs directory
      struct stat st;
      if (stat(path.Get(), &st) == 0 && S_ISDIR(st.st_mode)) {
        return FilesystemResult::AlreadyExistsAsDirectory;
      }
    }
    return MapErrnoToResult(error);
  }

  FilesystemResult ListFiles(
      const PlatformPath& path, std::vector<std::string>& out_names
  ) override {
    out_names.clear();

    // Open directory for reading. If directory doesn't exist, return OK with
    // empty vector (as specified in plan).
    DIR* dir = opendir(path.Get());
    if (dir == nullptr) {
      if (errno == ENOENT) {
        return FilesystemResult::OK;  // Missing directory = empty list
      }
      return MapErrnoToResult(errno);
    }

    // Iterate through directory entries, filtering for regular files
    while (true) {
      errno = 0;
      struct dirent* entry = readdir(dir);
      if (entry == nullptr) {
        // Check if we hit an error or just reached end of directory
        const int error = errno;
        closedir(dir);
        return error == 0 ? FilesystemResult::OK : MapErrnoToResult(error);
      }

      // Filter for regular files only. d_type is reliably populated on
      // modern local filesystems (ext4, XFS, APFS, HFS+).
      if (entry->d_type == DT_REG) {
        out_names.emplace_back(entry->d_name);
      }
    }
  }

  FilesystemResult ListSubdirectories(
      const PlatformPath& path, std::vector<std::string>& out_names
  ) override {
    out_names.clear();

    // Open directory for reading
    DIR* dir = opendir(path.Get());
    if (dir == nullptr) {
      if (errno == ENOENT) {
        return FilesystemResult::OK;  // Missing directory = empty list
      }
      return MapErrnoToResult(errno);
    }

    // Iterate through directory entries, filtering for subdirectories
    while (true) {
      errno = 0;
      struct dirent* entry = readdir(dir);
      if (entry == nullptr) {
        const int error = errno;
        closedir(dir);
        return error == 0 ? FilesystemResult::OK : MapErrnoToResult(error);
      }

      // Skip "." and ".." entries
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
        continue;
      }

      // Filter for directories only. d_type is reliably populated on
      // modern local filesystems (ext4, XFS, APFS, HFS+).
      if (entry->d_type == DT_DIR) {
        out_names.emplace_back(entry->d_name);
      }
    }
  }

  // === File Operations ===

  OpenFileResult OpenForWrite(
      const PlatformPath& path, bool append, bool hold_advisory_lock
  ) override {
    // Construct open flags: always wronly + creat, then append or trunc
    int flags = O_WRONLY | O_CREAT;
    flags |= append ? O_APPEND : O_TRUNC;

    // Open file with permissions 0600 (owner read/write only).
    // Retry on EINTR (signal interruption).
    int fd = -1;
    while (true) {
      fd = open(path.Get(), flags, 0600);
      if (fd >= 0) {
        break;  // Success
      }
      if (errno != EINTR) {
        // Failed for reason other than interrupt
        return {MapErrnoToResult(errno), INVALID_FILE_HANDLE};
      }
      // errno == EINTR: retry
    }

    // If advisory lock requested, attempt non-blocking exclusive lock
    if (hold_advisory_lock) {
      // flock() with LOCK_EX (exclusive) | LOCK_NB (non-blocking)
      // Returns 0 on success, -1 on failure
      while (true) {
        const int lock_result = flock(fd, LOCK_EX | LOCK_NB);
        if (lock_result == 0) {
          break;  // Lock acquired
        }
        if (errno == EINTR) {
          continue;  // Retry on interrupt
        }
        // Lock failed - close fd and return error
        const int lock_error = errno;
        close(fd);  // Ignore close errors here
        return {MapErrnoToResult(lock_error), INVALID_FILE_HANDLE};
      }
    }

    return {FilesystemResult::OK, fd};
  }

  OpenFileResult OpenForRead(
      const PlatformPath& path, bool acquire_advisory_lock
  ) override {
    // Open file read-only, retry on EINTR
    int fd = -1;
    while (true) {
      fd = open(path.Get(), O_RDONLY);
      if (fd >= 0) {
        break;  // Success
      }
      if (errno != EINTR) {
        return {MapErrnoToResult(errno), INVALID_FILE_HANDLE};
      }
    }

    // If advisory lock requested, attempt non-blocking shared lock
    if (acquire_advisory_lock) {
      // flock() with LOCK_SH (shared) | LOCK_NB (non-blocking)
      while (true) {
        const int lock_result = flock(fd, LOCK_SH | LOCK_NB);
        if (lock_result == 0) {
          break;  // Lock acquired
        }
        if (errno == EINTR) {
          continue;  // Retry on interrupt
        }
        // Lock failed
        const int lock_error = errno;
        close(fd);
        return {MapErrnoToResult(lock_error), INVALID_FILE_HANDLE};
      }
    }

    return {FilesystemResult::OK, fd};
  }

  WriteResult Write(PlatformFileHandle file, const char* src, size_t n) override {
    // Write exactly N bytes, looping on partial writes and EINTR.
    // This ensures all data is written even if write() returns < n.
    size_t total = 0;
    while (total < n) {
      const ssize_t result = write(file, src + total, n - total);
      if (result < 0) {
        if (errno == EINTR) {
          continue;  // Retry on interrupt
        }
        // Write error - return partial bytes written
        return {MapErrnoToResult(errno), total};
      }
      if (result == 0) {
        // Unexpected: write() returned 0 (should not happen for regular files)
        return {FilesystemResult::UnknownError, total};
      }
      total += static_cast<size_t>(result);
    }
    return {FilesystemResult::OK, total};
  }

  ReadResult Read(PlatformFileHandle file, char* dst, size_t n) override {
    // Read up to N bytes in a single syscall (no looping except for EINTR).
    // Returns actual bytes read, which may be less than N (or 0 for EOF).
    while (true) {
      const ssize_t result = read(file, dst, n);
      if (result < 0) {
        if (errno == EINTR) {
          continue;  // Retry on interrupt
        }
        return {MapErrnoToResult(errno), 0};
      }
      // result >= 0: success, may be 0 (EOF) or less than n
      return {FilesystemResult::OK, static_cast<size_t>(result)};
    }
  }

  FilesystemResult Close(PlatformFileHandle file) override {
    // Close file descriptor, retry on EINTR. Advisory locks are automatically
    // released by the kernel when fd is closed.
    while (true) {
      const int result = close(file);
      if (result == 0) {
        return FilesystemResult::OK;
      }
      if (errno != EINTR) {
        return MapErrnoToResult(errno);
      }
      // errno == EINTR: retry
    }
  }

  FilesystemResult Delete(const PlatformPath& path) override {
    // Delete regular file. Returns error if path is a directory or doesn't exist.
    const int result = unlink(path.Get());
    if (result == 0) {
      return FilesystemResult::OK;
    }
    return MapErrnoToResult(errno);
  }

  FilesystemResult Rename(const PlatformPath& src, const PlatformPath& dst) override {
    // Atomically rename src to dst. POSIX rename() clobbers dst by default,
    // but we want non-clobbering semantics, so check if dst exists first.
    // This introduces a small TOCTOU window, but it's acceptable for our use case
    // (batch file management where concurrent renames to same dst are unlikely).
    struct stat st;
    if (stat(dst.Get(), &st) == 0) {
      // Destination exists - return error to prevent clobber
      return FilesystemResult::AlreadyExists;
    }
    // stat() failed - if dst doesn't exist (ENOENT), proceed with rename.
    // For other errors (permission denied, etc.), proceed anyway and let
    // rename() report the error.

    // Attempt rename. Will fail if src doesn't exist, or if src and dst are
    // on different filesystems (EXDEV).
    const int result = rename(src.Get(), dst.Get());
    if (result == 0) {
      return FilesystemResult::OK;
    }

    return MapErrnoToResult(errno);
  }
};

}  // namespace datadog::impl

// NOLINTEND(cppcoreguidelines-pro-type-vararg)
// NOLINTEND(cppcoreguidelines-pro-type-member-init)
// NOLINTEND(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
// NOLINTEND(concurrency-mt-unsafe)
