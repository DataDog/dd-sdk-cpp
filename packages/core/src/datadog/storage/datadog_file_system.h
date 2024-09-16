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
// IDatadogFile implementaitons should catch (and not rethrow) exceptions, and
// instead set and return this file status.
enum class DatadogFileStatus {
  // Last operation succeeded, or the file is in a good state
  Ok,
  // The last operation (read or write) failed, but the file is still usable
  OperationFailure,
  // The last operation failed, and the failure is non-recoverable.
  BadState,
  // Currently at EoF
  EndOfFile,
};

// Interface class for file operations with Datadog. The default implementation
// is StdDatadogFile which uses classes from the C++ standard library.
class IDatadogFile {
 public:
  IDatadogFile() {}
  virtual ~IDatadogFile() = default;

  // Prevent copy and move, as file destructors will close the file
  IDatadogFile(const IDatadogFile&) = delete;
  IDatadogFile& operator=(const IDatadogFile&) = delete;
  IDatadogFile(IDatadogFile&&) = delete;
  IDatadogFile& operator=(IDatadogFile&&) = delete;

  // Get the size of the file in bytes
  virtual uintmax_t GetSize() const = 0;

  // Get the current status of the file
  virtual DatadogFileStatus GetStatus() const = 0;

  // Write the specified buffer to the file.
  virtual DatadogFileStatus Write(std::string_view buffer) = 0;

  // Read the contents of the file into a buffer, up to Size.
  // The number of bytes read is written to the bytes_read parameter.
  // cpp20: drop std::array and char* in favor of std::span
  template <size_t Size>
  DatadogFileStatus Read(std::array<char, Size>& buffer, size_t& bytes_read) {
    bytes_read = Size;
    return Read(buffer.data(), bytes_read);
  }

  // Read up from the file into a buffer.  The size of the buffer should
  // be passed to `bytes`, and the number of bytes read will be written
  // to `bytes` on a successful return.
  virtual DatadogFileStatus Read(char* buffer, size_t& bytes) = 0;
};

// Interface class for interacting with the filesystem. Allows clients to
// override methods for getting file system information, including where
// to store Datadog cache files.
//
// Implementers of IDatadogFileSystem should ensure the class cannot access
// files outside of its "BaseDirectory" (the path returned from
// GetBaseDirectory).
//
// Default implementation is SdtDatadogFileSystem, which uses the C++
// standard library to implement file operations.
class IDatadogFileSystem {
 public:
  virtual ~IDatadogFileSystem() = default;

  // Get the base directory for this file system. This directory must be
  // writable by the current process, and it will be created if it does not
  // exist.
  virtual const std::filesystem::path& GetBaseDirectory() = 0;

  // Open a file at the specified path and return it. Can return `nullptr`
  // if there is an error opening the file.
  virtual std::unique_ptr<IDatadogFile> OpenFile(
      const std::filesystem::path& path) = 0;

  // Delete a file at the specified path. Returns true if the file was
  // deleted successfully, false otherwise, including if the file did not
  // exist.
  virtual bool DeleteFile(const std::filesystem::path& path) = 0;

  // List all files under the specified path, non-recursive. Paths returned
  // should be relative to the file system's root path (the path returned
  // from GetBaseDirectory).
  virtual std::vector<std::filesystem::path> GetFiles(
      const std::filesystem::path& in_dir) = 0;
};

// Default implementation of IDatadogFileSystem, which uses the C++ standard
// library to implement file operations.
class StdDatadogFileSystem : public IDatadogFileSystem {
 public:
  explicit StdDatadogFileSystem(
      const std::filesystem::path& base_cache_directory = {"caches/datadog"});

  const std::filesystem::path& GetBaseDirectory() override;

  std::unique_ptr<IDatadogFile> OpenFile(
      const std::filesystem::path& path) override;

  bool DeleteFile(const std::filesystem::path& path) override;

  std::vector<std::filesystem::path> GetFiles(
      const std::filesystem::path& in_dir) override;

 private:
  bool IsInFileSystem(const std::filesystem::path& path);

  std::filesystem::path base_cache_directory_;
};

}  // namespace datadog::core::storage
