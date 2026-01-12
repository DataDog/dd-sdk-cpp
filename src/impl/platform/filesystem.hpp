// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cinttypes>
#include <memory>
#include <string_view>

#include "error_message.hpp"
#include "nonstd/expected.hpp"

namespace datadog::platform {

enum class FilesystemError : uint8_t {
  /**
   * The target file or directory does not exist.
   */
  DoesNotExist,
  /**
   * The target file or directory already exists.
   */
  AlreadyExists,
  /**
   * The operation failed, but the target file or directory is stil in a usable state.
   */
  Failed,
  /**
   * A low-level file I/O error occurred, and the target file or directory is not in a
   * usable state.
   */
  IOError,
};

/**
 * Result of a filesystem operation. If the operation succeeded, `has_value()` evaluates
 * true, and dereferencing the result yields the return value T. Otherwise, `error()`
 * yields a FilesystemError value indicating the nature of the failure.
 */
template <typename T>
using FilesystemResult = nonstd::expected<T, FilesystemError>;

/**
 * Handle to a binary file that's currently open for read.
 */
class IFileReader {
 protected:
  IFileReader() = default;

 public:
  virtual ~IFileReader() = default;

  // File-reader interfaces will never be copied
  IFileReader(const IFileReader&) = delete;
  IFileReader& operator=(const IFileReader&) = delete;

  // File-reader interfaces may be moved
  IFileReader(IFileReader&&) = default;
  IFileReader& operator=(IFileReader&&) = delete;

  /**
   * Seeks forward or backward in the file by the given number of bytes.
   *
   * If the operation seeks forward past the end of the file, no error will result until
   * the next read, and Seek will return without error.
   *
   * If the operation would seek backward past the _beginning_ of the file, Seek will
   * return FilesystemError::Failed.
   *
   * @param offset Number of bytes to move relative to the current position in the file.
   *  May be negative to indicate a backward offset.
   */
  virtual FilesystemResult<void> Seek(int offset) = 0;

  /**
   * Reads up to `n` bytes from the file into `dst`.
   *
   * @returns On successful read, the number of bytes actually read from the file. A
   *  return value less than `n` indicates EOF. If the read operation fails, returns a
   *  FilesystemError.
   */
  virtual FilesystemResult<size_t> Read(char* dst, size_t n) = 0;
};

/**
 * Handle to a binary file that may be written to. Implies exclusive write access to the
 * underlying file: no two IFileWriter instances will be in use for the same file.
 *
 * To ensure durable event storage is and relatively simple synchronization between
 * writer and reader threads, we use a close-after-write approach: every file write
 * opens the file, appends the relevant data, then closes the file to flush it to disk.
 */
class IFileWriter {
 protected:
  IFileWriter() = default;

 public:
  virtual ~IFileWriter() = default;

  // File-writer interfaces will never be copied
  IFileWriter(const IFileWriter&) = delete;
  IFileWriter& operator=(const IFileWriter&) = delete;

  // File-writer interfaces may be moved
  IFileWriter(IFileWriter&&) = default;
  IFileWriter& operator=(IFileWriter&&) = delete;

  /**
   * Opens the file for append (a la `fopen("ab")`), writes the provided n bytes at the
   * end of the file, then closes the file, ensuring that the write is flushed to disk.
   */
  virtual FilesystemResult<void> Write(const char* src, size_t n) = 0;
};

/**
 * Handle to a directory within the Datadog storage root. Permits access to files and
 * subdirectories that are direct children of this directory. All `name` parameters are
 * given as basename only, e.g. "foo.dat" is valid; "foo/bar.dat", "../foo.dat",
 * "/foo.dat", etc. are invalid and will never be supplied.
 */
class IDirectory {
 protected:
  IDirectory() = default;

 public:
  virtual ~IDirectory() = default;

  // Noncopyable, movable
  IDirectory(const IDirectory&) = delete;
  IDirectory& operator=(const IDirectory&) = delete;
  IDirectory(IDirectory&&) = default;
  IDirectory& operator=(IDirectory&&) = default;

  /**
   * Populates the provided vector with the names of all regular files that exist in
   * this directory.
   *
   * @param out_names Reference to an empty vector to be populated with filenames.
   */
  virtual FilesystemResult<void> ListFiles(
      std::vector<std::string>& out_names
  ) const = 0;

  /**
   * Deletes the file with the given name. If the file does not exist, returns
   * FilesystemError::DoesNotExist.
   */
  virtual FilesystemResult<void> RemoveFile(std::string_view name) = 0;

  /**
   * Given a source file in this directory with the given name, attempts to move that
   * file into the destination directory given as `dst_directory_path`. If the
   * destination directory does not yet exist, this operation will attempt to create it
   * implicitly.
   *
   * If the source file does not exist, returns FilesystemError::DoesNotExist. If the
   * destination directory already contains a file with the given name, returns
   * FilesystemError::AlreadyExists.
   */
  virtual FilesystemResult<void> MoveFile(
      std::string_view name, std::string_view dst_directory_path
  ) = 0;

  /**
   * Opens an existing file for read, in binary mode, a la `fopen(name, "rb")`. If the
   * file does not exist, returns FilesystemError::DoesNotExist.
   */
  virtual FilesystemResult<std::unique_ptr<IFileReader>> OpenForRead(
      std::string_view name
  ) = 0;

  /**
   * Constructs a new wrapper for a file at the given path. Does not actually open the
   * file, but ensures that all parent directories exist.
   */
  virtual FilesystemResult<std::unique_ptr<IFileWriter>> PrepareForWrite(
      std::string_view name
  ) = 0;

  /**
   * Returns a handle to a new or existing subdirectory with the given name.
   */
  virtual FilesystemResult<std::unique_ptr<IDirectory>> PrepareSubdirectory(
      std::string_view name
  ) = 0;
};

/**
 * Handle to the root directory where Datadog is permitted to write files.
 */
class IStorageDirectory : public virtual IDirectory {
 protected:
  IStorageDirectory() = default;

 public:
  /**
   * Cleans up any global state maintained by the filesystem implementation.
   */
  ~IStorageDirectory() override = default;

  // Noncopyable, movable
  IStorageDirectory(const IStorageDirectory&) = delete;
  IStorageDirectory& operator=(const IStorageDirectory&) = delete;
  IStorageDirectory(IStorageDirectory&&) = default;
  IStorageDirectory& operator=(IStorageDirectory&&) = default;
};

namespace Filesystem {
using InitResult =
    nonstd::expected<std::unique_ptr<IStorageDirectory>, datadog::impl::ErrorMessage>;

InitResult Init(std::string_view path);
};  // namespace Filesystem

}  // namespace datadog::platform
