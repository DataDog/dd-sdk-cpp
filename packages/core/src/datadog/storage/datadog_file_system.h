// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.
#pragma once

#include <filesystem>
#include <memory>
#include <vector>

namespace datadog::core::storage {

// Base class for file operations with Datadog. The default implementation
// is StdDatadogFile which uses classes from the C++ standard library.
class IDatadogFile {
 public:
  virtual ~IDatadogFile() {}

  // TODO(jeff.ward): Add template methods to simplify writing, or inherit
  // from istream / ostream?
  virtual void Write(const char* const buffer, size_t buffer_size) = 0;
};

// Base class for interacting with the filesystem. Allows clients to
// override methods for getting file system information, including where
// to store Datadog cache files.
//
// Default implementation is SdtDatadogFileSystem, which uses the C++
// standard library to implement file operations
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

  // Delete a file at the specified path. Should fail silently if the file
  // does not exist.
  virtual void DeleteFile(const std::filesystem::path& path) = 0;

  // List all files under the specified path, non-recursive.
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

  void DeleteFile(const std::filesystem::path& path) override;

  std::vector<std::filesystem::path> GetFiles(
      const std::filesystem::path& in_dir) override;

 private:
  std::filesystem::path base_cache_directory_;
};

}  // namespace datadog::core::storage
