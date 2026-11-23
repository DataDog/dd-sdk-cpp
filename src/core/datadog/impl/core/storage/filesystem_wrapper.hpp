// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include "datadog/impl/core/storage/filesystem.hpp"
#include "datadog/impl/core/storage/path.hpp"

namespace datadog::impl {

/**
 * RAII wrapper for a PlatformFileHandle, which will ensure that the file is closed upon
 * leaving scope.
 */
class File {
 private:
  IFilesystem& _fs;
  PlatformFileHandle _handle;

 public:
  /**
   * Initializes a wrapper that will own the given file handle.
   */
  explicit File(IFilesystem& in_fs, PlatformFileHandle in_handle);

  /**
   * Closes the file if it hasn't already been closed.
   */
  ~File();

  // Noncopyable, movable via move constructor for NRVO
  File(const File&) = delete;
  File& operator=(const File&) = delete;
  File(File&&) noexcept;
  File& operator=(File&&) = delete;

  // Wrappers for IFilesystem methods
  IFilesystem::WriteResult Write(const char* src, size_t n);

  /**
   * Reads exactly `n` bytes into `dst`, retrying across multiple underlying
   * IFilesystem::Read calls until all bytes are consumed or EOF is reached.
   *
   * On success, returns {OK, n}. If EOF is reached before `n` bytes are available,
   * returns {OK, k} where k < n is the number of bytes read before EOF. On a
   * filesystem error, returns {error, k} where k is the number of bytes read before
   * the error.
   */
  IFilesystem::ReadResult Read(char* dst, size_t n);
  FilesystemResult Close();
};

/**
 * Syntactic-sugar wrapper for IFilesystem that allows file operations to be performed
 * with UTF-8 strings, without requring explicit PlatformPath encoding on each call, and
 * which returns `File` wrappers rather than raw handles.
 *
 * Encoding path values into a PlatformPath buffer only has an effect on Windows, where
 * it converts a UTF-8 string to UTF-16. This can only fail if the input value is not a
 * valid UTF-8 string or exceeds the buffer size. For internal SDK usage, where we use
 * fixed-size StoragePath buffers and simple directory/file names, neither of these
 * cases is possible.
 *
 * This wrapper class handles the rare and unexpected case of encoding failure by
 * returning FilesystemError::PathEncodingFailed. If encoding fails, no actual
 * IFilesystem calls are made.
 */
class FilesystemWrapper {
 private:
  IFilesystem& _fs;
  PlatformPath _path;

 public:
  /**
   * Initializes a wrapper that will make IFilesystem calls using the provided
   * reference, reusing an internal PlatformPath buffer as necessary for path encoding.
   *
   * A FilesystemWrapper interface, and any File objects created on open, must not
   * outlive the associated IFilesystem.
   */
  explicit FilesystemWrapper(IFilesystem& in_fs);

  struct OpenFileResult {
    FilesystemResult value;
    File file;
  };

  // Wrappers for IFilesystem methods
  FilesystemResult CreateDirectory(const char* path);
  FilesystemResult ListFiles(const char* path, std::vector<std::string>& out_names);
  FilesystemResult ListSubdirectories(
      const char* path, std::vector<std::string>& out_names
  );
  OpenFileResult OpenForWrite(const char* path, bool append, bool hold_advisory_lock);
  OpenFileResult OpenForRead(const char* path, bool hold_advisory_lock);
  IFilesystem::GetFileSizeResult GetFileSize(const char* path);
  FilesystemResult Delete(const char* path);
  FilesystemResult DeleteDirectory(const char* path);
  FilesystemResult Rename(const char* src, const char* dst);
  FilesystemResult ReplaceFile(const char* src, const char* dst);
};

}  // namespace datadog::impl
