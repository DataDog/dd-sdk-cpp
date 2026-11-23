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

#include <climits>
#include <cstring>
#include <memory>

#include "datadog/impl/core/storage/filesystem.hpp"

// NOLINTBEGIN(concurrency-mt-unsafe)
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
// NOLINTBEGIN(cppcoreguidelines-pro-type-member-init)
// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg)

namespace datadog::impl {

static FilesystemResult map_errno(int err) {
  switch (err) {
    case ENOENT:
      return FilesystemResult::DoesNotExist;
    case EEXIST:
      return FilesystemResult::AlreadyExists;
    case ENOTEMPTY:
      return FilesystemResult::DirectoryNotEmpty;
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

class PosixFilesystem final : public IFilesystem {
 public:
  FilesystemResult CreateDirectory(const PlatformPath& path) override {
    // Attempt to create directory with rwx------ permissions
    const int result = mkdir(path.Get(), 0700);
    if (result == 0) {
      return FilesystemResult::OK;
    }

    // If creation failed due to an existing file at the target path, use stat() to see
    // if it's a file or a directory
    const int error = errno;
    if (error == EEXIST) {
      struct stat st;
      if (stat(path.Get(), &st) == 0 && S_ISDIR(st.st_mode)) {
        return FilesystemResult::AlreadyExistsAsDirectory;
      }
    }
    return map_errno(error);
  }

  FilesystemResult ListFiles(
      const PlatformPath& path, std::vector<std::string>& out_names
  ) override {
    // Clear output vector
    out_names.clear();

    // Open directory for reading
    DIR* dir = opendir(path.Get());
    if (dir == nullptr) {
      return map_errno(errno);
    }

    // Iterate through directory entries, filtering for regular files
    while (true) {
      errno = 0;
      struct dirent* entry = readdir(dir);
      if (entry == nullptr) {
        // Check if we hit an error or just reached end of directory
        const int error = errno;
        closedir(dir);
        return error == 0 ? FilesystemResult::OK : map_errno(error);
      }

      // d_type is reliably populated on modern local filesystems (ext4, XFS, APFS,
      // HFS+), but on NFS and some other filesystems it may be DT_UNKNOWN. Fall back
      // to stat() in that case.
      if (entry->d_type == DT_UNKNOWN) {
        char full[MAX_STORAGE_PATH_SIZE + NAME_MAX + 2];
        snprintf(full, sizeof(full), "%s/%s", path.Get(), entry->d_name);
        struct stat st;
        if (lstat(full, &st) != 0 || !S_ISREG(st.st_mode)) {
          continue;
        }
        out_names.emplace_back(entry->d_name);
        continue;
      }
      if (entry->d_type == DT_REG) {
        out_names.emplace_back(entry->d_name);
      }
    }
  }

  FilesystemResult ListSubdirectories(
      const PlatformPath& path, std::vector<std::string>& out_names
  ) override {
    // Clear output vector
    out_names.clear();

    // Open directory for reading
    DIR* dir = opendir(path.Get());
    if (dir == nullptr) {
      return map_errno(errno);
    }

    // Iterate through directory entries, filtering for subdirectories
    while (true) {
      errno = 0;
      struct dirent* entry = readdir(dir);
      if (entry == nullptr) {
        const int error = errno;
        closedir(dir);
        return error == 0 ? FilesystemResult::OK : map_errno(error);
      }

      // Skip "." and ".." entries
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
        continue;
      }

      // d_type is reliably populated on modern local filesystems (ext4, XFS, APFS,
      // HFS+), but on NFS and some other filesystems it may be DT_UNKNOWN. Fall back
      // to stat() in that case.
      if (entry->d_type == DT_UNKNOWN) {
        char full[MAX_STORAGE_PATH_SIZE + NAME_MAX + 2];
        snprintf(full, sizeof(full), "%s/%s", path.Get(), entry->d_name);
        struct stat st;
        if (lstat(full, &st) != 0 || !S_ISDIR(st.st_mode)) {
          continue;
        }
        out_names.emplace_back(entry->d_name);
        continue;
      }
      if (entry->d_type == DT_DIR) {
        out_names.emplace_back(entry->d_name);
      }
    }
  }

  OpenFileResult OpenForWrite(
      const PlatformPath& path, bool append, bool hold_advisory_lock
  ) override {
    // When appending, include O_APPEND so writes go to end of file. When not
    // appending, omit O_TRUNC here - truncation must be deferred until after the
    // advisory lock is acquired, to avoid destroying file contents on contention
    int flags = O_WRONLY | O_CREAT;
    if (append) {
      flags |= O_APPEND;
    }

    // Open file with permissions 0600 (owner read/write only), and retry on EINTR
    int fd = -1;
    while (true) {
      fd = open(path.Get(), flags, 0600);
      if (fd >= 0) {
        break;
      }
      if (errno == EINTR) {
        continue;
      }
      return {map_errno(errno), INVALID_FILE_HANDLE};
    }

    // If advisory lock requested, attempt non-blocking exclusive lock
    if (hold_advisory_lock) {
      while (true) {
        const int lock_result = flock(fd, LOCK_EX | LOCK_NB);
        if (lock_result == 0) {
          break;
        }
        if (errno == EINTR) {
          continue;
        }
        // Lock failed: close fd and return error
        const int lock_error = errno;
        close(fd);
        return {map_errno(lock_error), INVALID_FILE_HANDLE};
      }
    }

    // If the caller doesn't want append semantics, truncate the file now that we're
    // past the advisory-lock check
    if (!append) {
      // This is equivalent to opening with O_TRUNC
      while (true) {
        if (ftruncate(fd, 0) == 0) {
          break;
        }
        if (errno == EINTR) {
          continue;
        }
        const int trunc_error = errno;
        close(fd);
        return {map_errno(trunc_error), INVALID_FILE_HANDLE};
      }
    }

    return {FilesystemResult::OK, fd};
  }

  OpenFileResult OpenForRead(
      const PlatformPath& path, bool hold_advisory_lock
  ) override {
    // Open file read-only; retry on EINTR
    int fd = -1;
    while (true) {
      fd = open(path.Get(), O_RDONLY);
      if (fd >= 0) {
        break;
      }
      if (errno == EINTR) {
        continue;
      }
      return {map_errno(errno), INVALID_FILE_HANDLE};
    }

    // If advisory lock requested, attempt non-blocking exclusive lock
    if (hold_advisory_lock) {
      while (true) {
        const int lock_result = flock(fd, LOCK_EX | LOCK_NB);
        if (lock_result == 0) {
          break;
        }
        if (errno == EINTR) {
          continue;
        }
        // Lock failed: close file and return error from flock() call
        const int lock_error = errno;
        close(fd);
        return {map_errno(lock_error), INVALID_FILE_HANDLE};
      }
    }

    return {FilesystemResult::OK, fd};
  }

  WriteResult Write(PlatformFileHandle handle, const char* src, size_t n) override {
    // Write exactly N bytes, looping to retry on partial writes and EINTR, ensuring
    // that all data is written even if a write() call returns < n
    size_t total = 0;
    while (total < n) {
      const ssize_t result = write(handle, src + total, n - total);
      if (result < 0) {
        if (errno == EINTR) {
          continue;
        }
        return {map_errno(errno), total};
      }
      if (result == 0) {
        // Unexpected: write() returned 0 (should not happen for regular files)
        return {FilesystemResult::UnknownError, total};
      }
      total += static_cast<size_t>(result);
    }
    return {FilesystemResult::OK, total};
  }

  ReadResult Read(PlatformFileHandle handle, char* dst, size_t n) override {
    // Read up to N bytes in a single syscall (no looping except to retry in case of
    // EINTR)
    while (true) {
      const ssize_t result = read(handle, dst, n);
      if (result < 0) {
        if (errno == EINTR) {
          continue;
        }
        return {map_errno(errno), 0};
      }
      // result >= 0: success, may be 0 (EOF) or less than n
      return {FilesystemResult::OK, static_cast<size_t>(result)};
    }
  }

  FilesystemResult Close(PlatformFileHandle handle) override {
    // On Linux, close() always releases the fd even when it returns EINTR — retrying
    // would close an already-closed (and potentially reused) fd. Treat EINTR as success
    // on all POSIX platforms; the fd is gone either way. Advisory locks are released
    // automatically when the fd is closed.
    if (close(handle) == 0 || errno == EINTR) {
      return FilesystemResult::OK;
    }
    return map_errno(errno);
  }

  GetFileSizeResult GetFileSize(const PlatformPath& path) override {
    struct stat st;
    if (stat(path.Get(), &st) != 0) {
      return {map_errno(errno), 0};
    }
    return {FilesystemResult::OK, static_cast<size_t>(st.st_size)};
  }

  FilesystemResult Delete(const PlatformPath& path) override {
    const int result = unlink(path.Get());
    if (result == 0) {
      return FilesystemResult::OK;
    }
    return map_errno(errno);
  }

  FilesystemResult DeleteDirectory(const PlatformPath& path) override {
    const int result = rmdir(path.Get());
    if (result == 0) {
      return FilesystemResult::OK;
    }
    return map_errno(errno);
  }

  FilesystemResult Rename(const PlatformPath& src, const PlatformPath& dst) override {
    // Atomically rename src to dst. POSIX rename() clobbers dst by default, but we want
    // non-clobbering semantics, so check if dst exists first. This introduces a small
    // TOCTOU window, but it's acceptable for our use case.
    struct stat st;
    if (stat(dst.Get(), &st) == 0) {
      // Destination exists - return error to prevent clobber
      return FilesystemResult::AlreadyExists;
    }
    // stat() failed, either because the destination file doesn't exist (ENOENT, as
    // expected) or because of another error. Either way, proceed with the rename: any
    // unexpected errors will be reported by rename().
    const int result = rename(src.Get(), dst.Get());
    if (result == 0) {
      return FilesystemResult::OK;
    }
    return map_errno(errno);
  }

  FilesystemResult ReplaceFile(
      const PlatformPath& src, const PlatformPath& dst
  ) override {
    // Atomically rename src to dst. POSIX rename() clobbers dst by default.
    const int result = rename(src.Get(), dst.Get());
    if (result == 0) {
      return FilesystemResult::OK;
    }
    return map_errno(errno);
  }
};

std::unique_ptr<IFilesystem> CreateFilesystem() {
  return std::make_unique<PosixFilesystem>();
}

}  // namespace datadog::impl

// NOLINTEND(cppcoreguidelines-pro-type-vararg)
// NOLINTEND(cppcoreguidelines-pro-type-member-init)
// NOLINTEND(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
// NOLINTEND(concurrency-mt-unsafe)
