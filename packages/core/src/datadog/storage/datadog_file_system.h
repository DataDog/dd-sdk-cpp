// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.
#pragma once

#include <array>
#include <filesystem>
#include <memory>
#include <vector>

namespace datadog::core::storage {

// Current state of a file, and the return value for operations on files.
// DatadogFile implementaitons should catch (and not rethrow) exceptions, and
// instead set and return this file status.
enum class DatadogFileStatus {
  // Last operation succeeded, or the file is in a good state.
  Ok,
  // The last operation (read or write) failed, but the file is still usable.
  OperationFailure,
  // The last operation result is no changes, for example when attempting to
  // delete a non-existant file.
  NoOperation,
  // The last operation failed, and the failure is non-recoverable.
  BadState,
  // The last operation attempted to read past the end of the file.
  EndOfFile,
};

// Interface class for file operations with Datadog. The default implementation
// is StdDatadogFile which uses classes from the C++ standard library.
class DatadogFile {
 public:
  DatadogFile(const std::filesystem::path& path) : path_(path) {}
  virtual ~DatadogFile() = default;

  // Prevent copy and move, as file destructors will close the file.
  DatadogFile(const DatadogFile&) = delete;
  DatadogFile& operator=(const DatadogFile&) = delete;
  DatadogFile(DatadogFile&&) = delete;
  DatadogFile& operator=(DatadogFile&&) = delete;

  // Get the size of the file in bytes.
  virtual uintmax_t GetSize() const = 0;

  // Get the path of the file. The path will be relative to the
  // DatadogFileSystem that created it.
  const std::filesystem::path& GetPath() const { return path_; }

  // Get the current status of the file. This status is updated after a call to
  // `Read` or `Write`.
  virtual DatadogFileStatus GetStatus() const { return status_; }

  // Write the specified buffer to the file.
  virtual bool Write(std::string_view buffer) = 0;

  // Read the contents of the file into a buffer, up to Size. If the read
  // attempted to read past the end of the file, this method will return
  // DatadogFileStatus::EndOfFile, and the number of bytes read will be written
  // to `bytes_read`.
  // cpp20: drop std::array and char* in favor of std::span.
  template <size_t Size>
  bool ReadArray(std::array<char, Size>& buffer, size_t& bytes_read) {
    bytes_read = Size;
    return Read(buffer.data(), bytes_read);
  }

  // Read up from the file into a buffer. The size of the buffer should
  // be passed to `bytes`. If the read attempted to read past the end of the
  // file, this method will return `false`, set the status of the file to
  // `DatadogFileStatus::EndOfFile`, and number of bytes read will be written to
  // `bytes`.
  //
  // Passing a `nullptr` to this method will seek forward the number of `bytes`
  // into the file. The caller should take steps to not seek past the end of the
  // file.
  virtual bool Read(char* buffer, size_t& bytes) = 0;

 protected:
  DatadogFileStatus status_;
  const std::filesystem::path path_;
};

// Interface class for interacting with the filesystem. Allows clients to
// override methods for getting file system information, including where to
// store Datadog cache files.
//
// Implementers of DatadogFileSystem should ensure the class cannot access
// files outside of its area of control, such as outside of a base directory.
//
// While the default implemenation uses the physical file system, it is possible
// to implement this interface to use an entirely virtualized filesystem, such
// as inside of a database.
//
// The default implementation is StdDatadogFileSystem, which uses the C++
// standard library to implement file operations.
class DatadogFileSystem {
 public:
  virtual ~DatadogFileSystem() = default;

  // Open a file at the specified path and return it. Can return `nullptr`
  // if there is an error opening the file.
  virtual std::unique_ptr<DatadogFile> Open(
      const std::filesystem::path& path) = 0;

  // Check if a file exits at the specified path. This function should return
  // false if an attempt is made to check outside of the root of the file
  // system.
  virtual bool Exists(const std::filesystem::path& path) = 0;

  // Delete a file at the specified path, returning the result of the operation.
  // This function should not throw if the file does not exist, and should
  // instead return Datadog DatadogFileStatus::NoOperation. If a caller attempts
  // to read a file outside of the file system, it should not check for the
  // existance of the file, and instead return
  // DatadogFileStatus::OperationFailure.
  virtual DatadogFileStatus Delete(const std::filesystem::path& path) = 0;

  // TODO(jeff.ward): Add Exists

  // List all files under the specified path, non-recursive. Paths returned
  // should be relative to the file system's root path.
  //
  // Returns false if there was an error reading files in the requested path.
  virtual bool ListFiles(const std::filesystem::path& in_dir,
                         std::vector<std::filesystem::path>& files) = 0;
};

// Default implementation of DatadogFileSystem, which uses the C++ standard
// library to implement file operations.
class StdDatadogFileSystem : public DatadogFileSystem {
 public:
  explicit StdDatadogFileSystem(
      const std::filesystem::path& base_cache_directory = {"caches/datadog"});

  std::unique_ptr<DatadogFile> Open(const std::filesystem::path& path) override;

  bool Exists(const std::filesystem::path& path) override;

  DatadogFileStatus Delete(const std::filesystem::path& path) override;

  bool ListFiles(const std::filesystem::path& in_dir,
                 std::vector<std::filesystem::path>& files) override;

 private:
  bool IsInFileSystem(const std::filesystem::path& path);

  std::filesystem::path base_cache_directory_;
};

}  // namespace datadog::core::storage
