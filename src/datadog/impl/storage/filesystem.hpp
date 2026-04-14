// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <cinttypes>
#include <memory>
#include <string>
#include <vector>

#include "datadog/impl/storage/path.hpp"

namespace datadog::impl {

#ifdef _WIN32
using PlatformFileHandle = HANDLE;
static const PlatformFileHandle INVALID_FILE_HANDLE = INVALID_HANDLE_VALUE;
#else
using PlatformFileHandle = int;
static const PlatformFileHandle INVALID_FILE_HANDLE = -1;
#endif

/**
 * Result of a filesystem operation.
 *
 * These values are mapped from platform-specific error codes (errno, GetLastError()) to
 * provide consistent error handling across all platforms.
 */
enum class FilesystemResult : uint8_t {
  OK,
  AlreadyExistsAsDirectory,
  AlreadyExists,
  DoesNotExist,
  DirectoryNotEmpty,
  PermissionDenied,
  ReadOnlyFilesystem,
  OutOfSpace,
  PathTooLong,
  InvalidName,
  PathEncodingFailed,
  LockContention,
  UnknownError
};

/**
 * Interface for low-level filesystem operations.
 *
 * Uses the appropriate system APIs to implement file I/O, directory operations, and
 * advisory file locking as required by the SDK.
 *
 * All operations that require filesystem paths use PlatformPath, which provides direct
 * access to a null-terminated string in the appropriate representation for the platform
 * (UTF-16 on Windows; UTF-8 otherwise), without requiring heap allocation. File I/O
 * operations accept a valid PlatformFileHandle, which is defined as the canonical file
 * descriptor type for the platform (HANDLE on Windows; int otherwise).
 */
class IFilesystem {
 public:
  IFilesystem() = default;
  virtual ~IFilesystem() = default;

  // Noncopyable, movable
  IFilesystem(const IFilesystem&) = delete;
  IFilesystem& operator=(const IFilesystem&) = delete;
  IFilesystem(IFilesystem&&) = default;
  IFilesystem& operator=(IFilesystem&&) = default;

  /**
   * Creates a directory at the specified path.
   *
   * On POSIX systems, directories are created with permission flags 0700 (owner
   * read/write/execute only). On Windows, security attributes are inherited from the
   * parent directory.
   *
   * The parent directory MUST already exist: this function does not create nested
   * directories recursively. If any parent directory does not exist, returns
   * DoesNotExist.
   *
   * If the directory is created successfully, returns OK. If the directory is already
   * present, returns AlreadyExistsAsDirectory. Any other error code represents a
   * failure.
   */
  virtual FilesystemResult CreateDirectory(const PlatformPath& path) = 0;

  /**
   * Populates `out_names` with a list of basenames (no path prefix) for all regular
   * files in the given directory, in filesystem order (unsorted). Excludes
   * subdirectories, symlinks, and special files.
   *
   * Clears out_names before populating it.
   *
   * If the directory exists and a listing is successfully retrieved, returns OK and
   * populates out_names with 0 or more filenames. If no such directory exists, returns
   * DoesNotExist. Any other error code represents a failure.
   */
  virtual FilesystemResult ListFiles(
      const PlatformPath& path, std::vector<std::string>& out_names
  ) = 0;

  /**
   * Populates `out_names` with a list of basenames (no path prefix) for all
   * subdirectories contained in the given directory, in filesystem order (unsorted).
   * Excludes ".", "..", symlinks, and regular files.
   *
   * Clears out_names before populating it.
   *
   * If the directory exists and a listing is successfully retrieved, returns OK and
   * populates out_names with 0 or more subdirectory names. If no such directory exists,
   * returns DoesNotExist. Any other error code represents a failure.
   */
  virtual FilesystemResult ListSubdirectories(
      const PlatformPath& path, std::vector<std::string>& out_names
  ) = 0;

  struct OpenFileResult {
    FilesystemResult value;
    PlatformFileHandle handle;
  };

  /**
   * Opens a binary file for writing, optionally with advisory locking, creating the
   * file if it does not yet exist.
   *
   * On POSIX systems, newly-created files receive mode 0600 (read/write access for
   * owner only).
   *
   * If `append` is true, existing files will be opened in append mode, such that all
   * subsequent writes will start from the end of the file. If false, the file will be
   * truncated to zero length if it already exists.
   *
   * If `hold_advisory_lock` is true, the function will make a non-blocking attempt to
   * acquire an exclusive lock on the file. If unable to acquire a lock, closes the file
   * and returns LockContention. If a lock is acquired, it remains held until Close() is
   * called on the resulting file handle.
   *
   * On success, returns {OK, handle}. On failure, returns {error, INVALID_FILE_HANDLE}.
   */
  virtual OpenFileResult OpenForWrite(
      const PlatformPath& path, bool append, bool hold_advisory_lock
  ) = 0;

  /**
   * Opens a binary file for reading, optionally with advisory locking.
   *
   * File must already exist. If no file exists at the given path, returns DoesNotExist.
   *
   * If `hold_advisory_lock` is true, the function will make a non-blocking attempt to
   * acquire an exclusive lock on the file. If unable to acquire a lock, closes the file
   * and returns LockContention. If a lock is acquired, it remains held until Close() is
   * called on the resulting file handle.
   *
   * On success, returns {OK, handle}. On failure, returns {error, INVALID_FILE_HANDLE}.
   */
  virtual OpenFileResult OpenForRead(
      const PlatformPath& path, bool hold_advisory_lock
  ) = 0;

  struct WriteResult {
    FilesystemResult value;
    size_t bytes_written;
  };

  /**
   * Writes N bytes of binary data to a file that's been opened for write.
   *
   * If partial writes occur, retries in a loop to ensure that all N bytes are written.
   *
   * On success, returns {OK, n}. On failure, returns {error, num_bytes_written}, with
   * a num_bytes_written value greater than 0 indicating that a partial write occurred.
   */
  virtual WriteResult Write(PlatformFileHandle handle, const char* src, size_t n) = 0;

  struct ReadResult {
    FilesystemResult value;
    size_t bytes_read;
  };

  /**
   * Reads up to N bytes of binary data from a file that's been opened for read.
   *
   * `dst` must point to a contiguous buffer with capacity for at least N bytes.
   *
   * May return fewer than N bytes read when EOF has been reached or less data is
   * available than was requested.
   *
   * On success, returns {OK, bytes_read}, with a bytes_read value of 0 indicating EOF.
   * On failure, returns {error, 0}.
   */
  virtual ReadResult Read(PlatformFileHandle handle, char* dst, size_t n) = 0;

  /**
   * Closes an open file handle.
   *
   * Flushes any buffered writes and releases advisory locks if held.
   *
   * May return error if final flush fails.
   */
  virtual FilesystemResult Close(PlatformFileHandle handle) = 0;

  /**
   * Deletes a regular file.
   *
   * Returns OK on success, or an error code if the file doesn't exist, was a directory,
   * or couldn't be deleted.
   */
  virtual FilesystemResult Delete(const PlatformPath& path) = 0;

  /**
   * Deletes an empty directory at the specified path.
   *
   * Fails if the directory is non-empty; the caller must ensure all contents have been
   * removed before calling this function. Returns OK on success, DoesNotExist if no
   * directory exists at the path, and an error code if the directory is non-empty or
   * the operation fails.
   */
  virtual FilesystemResult DeleteDirectory(const PlatformPath& path) = 0;

  /**
   * Atomically renames a file, given two paths on the same filesystem.
   *
   * If a file or directory already exists at the destination path, refrains from moving
   * the file and returns AlreadyExists, leaving both source and destination files
   * intact.
   *
   * Returns OK on success, or an error code if the source file doesn't exist, the
   * destination path is occupied, the paths point to different filesystems, or the
   * rename operation failed.
   */
  virtual FilesystemResult Rename(const PlatformPath& src, const PlatformPath& dst) = 0;
};

/**
 * Creates a platform-specific filesystem implementation.
 *
 * Returns PosixFilesystem on POSIX systems, WindowsFilesystem on Windows.
 */
std::unique_ptr<IFilesystem> CreateFilesystem();

}  // namespace datadog::impl
