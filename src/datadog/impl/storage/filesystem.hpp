// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <cinttypes>
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

// === Filesystem Operation Results ===
// Error codes for filesystem operations. These are mapped from platform-specific
// errors (POSIX errno, Windows GetLastError) to provide consistent error reporting
// across platforms.
enum class FilesystemResult : uint8_t {
  OK,
  AlreadyExistsAsDirectory,
  AlreadyExists,
  ParentDirectoryDoesNotExist,
  PermissionDenied,
  ReadOnlyFilesystem,
  OutOfSpace,
  PathTooLong,
  InvalidName,
  LockContention,  // Advisory lock could not be acquired (non-blocking attempt)
  UnknownError
};

// === Filesystem Interface ===
// Low-level filesystem operations interface providing file I/O, directory
// operations, and advisory file locking. All path operations use StoragePath
// and PlatformPath to avoid heap allocations.
//
// Thread safety: Implementations are NOT thread-safe. Callers must synchronize
// access to shared IFilesystem instances.
//
// Error handling: All operations return FilesystemResult codes. File handles
// must be explicitly closed via Close() - no RAII wrapper is provided at this level.
class IFilesystem {
 public:
  IFilesystem() = default;
  virtual ~IFilesystem() = default;

  // Noncopyable, movable
  IFilesystem(const IFilesystem&) = delete;
  IFilesystem& operator=(const IFilesystem&) = delete;
  IFilesystem(IFilesystem&&) = default;
  IFilesystem& operator=(IFilesystem&&) = default;

  // === Directory Operations ===

  /**
   * Creates a directory at the specified path.
   *
   * Permissions (POSIX): 0700 (owner read/write/execute only)
   * Permissions (Windows): Inherit from parent directory
   *
   * @param path Platform-specific path to create
   * @return OK if created, AlreadyExistsAsDirectory if exists,
   *         ParentDirectoryDoesNotExist if parent missing, etc.
   */
  virtual FilesystemResult CreateDirectory(const PlatformPath& path) = 0;

  /**
   * Lists regular files in a directory (non-recursive).
   *
   * Returns basenames only (no path prefix), in filesystem order (unsorted).
   * Excludes subdirectories, symlinks, and special files.
   * Clears out_names before populating.
   *
   * @param path Directory to list
   * @param out_names Output vector for file basenames (cleared first)
   * @return OK on success (empty vector if directory empty/missing),
   *         error code on failure
   */
  virtual FilesystemResult ListFiles(
      const PlatformPath& path, std::vector<std::string>& out_names
  ) = 0;

  /**
   * Lists subdirectories in a directory (non-recursive).
   *
   * Returns basenames only (no path prefix), in filesystem order (unsorted).
   * Excludes "." and ".." entries, and regular files.
   * Clears out_names before populating.
   *
   * @param path Directory to list
   * @param out_names Output vector for directory basenames (cleared first)
   * @return OK on success (empty vector if directory empty/missing),
   *         error code on failure
   */
  virtual FilesystemResult ListSubdirectories(
      const PlatformPath& path, std::vector<std::string>& out_names
  ) = 0;

  // === File Operations ===

  struct OpenFileResult {
    FilesystemResult value;
    PlatformFileHandle handle;  // INVALID_FILE_HANDLE on error
  };

  /**
   * Opens a file for writing, optionally with advisory locking.
   *
   * Creates the file if it doesn't exist (mode 0600 on POSIX).
   * - append=true: Open in append mode (writes always go to end)
   * - append=false: Truncate to zero length
   * - hold_advisory_lock=true: Acquire exclusive non-blocking lock
   *   (POSIX: flock LOCK_EX|LOCK_NB, Windows: LockFileEx EXCLUSIVE)
   *
   * If lock requested but can't be acquired, returns LockContention.
   * Lock is held until Close() is called.
   *
   * @param path File to open/create
   * @param append If true, append to file; if false, truncate
   * @param hold_advisory_lock If true, acquire exclusive lock
   * @return {OK, handle} on success, {error, INVALID_FILE_HANDLE} on failure
   */
  virtual OpenFileResult OpenForWrite(
      const PlatformPath& path, bool append, bool hold_advisory_lock
  ) = 0;

  /**
   * Opens a file for reading, optionally with advisory locking.
   *
   * File must already exist.
   * - acquire_advisory_lock=true: Acquire shared non-blocking lock
   *   (POSIX: flock LOCK_SH|LOCK_NB, Windows: LockFileEx shared)
   *
   * If lock requested but can't be acquired, returns LockContention.
   * Lock is held until Close() is called.
   *
   * @param path File to open
   * @param acquire_advisory_lock If true, acquire shared lock
   * @return {OK, handle} on success, {error, INVALID_FILE_HANDLE} on failure
   */
  virtual OpenFileResult OpenForRead(
      const PlatformPath& path, bool acquire_advisory_lock
  ) = 0;

  struct WriteResult {
    FilesystemResult value;
    size_t bytes_written;  // Bytes written before error (may be < n on failure)
  };

  /**
   * Writes data to an open file.
   *
   * Loops internally on partial writes to ensure all N bytes are written.
   * For append-mode files, writes always go to end regardless of file position.
   *
   * @param file Open file handle from OpenForWrite
   * @param src Data to write
   * @param n Number of bytes to write
   * @return {OK, n} on success, {error, partial_bytes} on failure
   */
  virtual WriteResult Write(PlatformFileHandle file, const char* src, size_t n) = 0;

  struct ReadResult {
    FilesystemResult value;
    size_t bytes_read;  // Bytes actually read (0 = EOF, may be < n)
  };

  /**
   * Reads data from an open file (single syscall, no looping).
   *
   * Reads up to N bytes in a single operation. May return fewer bytes if:
   * - End of file reached (returns 0 bytes)
   * - Less data available than requested
   * Does NOT loop internally like Write().
   *
   * @param file Open file handle from OpenForRead
   * @param dst Buffer to read into (must have space for at least n bytes)
   * @param n Maximum number of bytes to read
   * @return {OK, bytes_read} on success (0 = EOF), {error, 0} on failure
   */
  virtual ReadResult Read(PlatformFileHandle file, char* dst, size_t n) = 0;

  /**
   * Closes an open file handle.
   *
   * Flushes any buffered writes and releases advisory locks if held.
   * May return error if final flush fails (e.g., disk full).
   *
   * @param file File handle to close
   * @return OK on success, error code on failure
   */
  virtual FilesystemResult Close(PlatformFileHandle file) = 0;

  /**
   * Deletes a regular file.
   *
   * Returns error if path is a directory or doesn't exist.
   *
   * @param path File to delete
   * @return OK on success, error code on failure
   */
  virtual FilesystemResult Delete(const PlatformPath& path) = 0;

  /**
   * Atomically renames a file (same filesystem only).
   *
   * Fails if destination already exists (no clobber).
   * Fails if source and destination are on different filesystems
   * (no cross-filesystem copy fallback).
   *
   * @param src Source file path
   * @param dst Destination file path
   * @return OK on success, AlreadyExists if dst exists, error otherwise
   */
  virtual FilesystemResult Rename(const PlatformPath& src, const PlatformPath& dst) = 0;
};

}  // namespace datadog::impl
