// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "datadog/impl/platform/filesystem.hpp"
#include "datadog/impl/storage/filesystem.hpp"
#include "datadog/impl/storage/path.hpp"

using namespace datadog;

// Adapts impl::FilesystemResult to platform::FilesystemError.
static platform::FilesystemError MapFilesystemError(impl::FilesystemResult r) {
  if (r == impl::FilesystemResult::DoesNotExist) {
    return platform::FilesystemError::DoesNotExist;
  }
  if (r == impl::FilesystemResult::AlreadyExists) {
    return platform::FilesystemError::AlreadyExists;
  }
  return platform::FilesystemError::Failed;
}

/**
 * IFileReader implementation backed by an IFilesystem handle, for use by the
 * upload thread when reading batch files via FilesystemDirectory.
 *
 * Note: Seek() is unimplemented since the upload thread only uses Read().
 */
class FilesystemFileReader : public platform::IFileReader {
 public:
  impl::IFilesystem& fs;
  impl::PlatformFileHandle handle;

  explicit FilesystemFileReader(
      impl::IFilesystem& in_fs, impl::PlatformFileHandle in_handle
  )
      : fs(in_fs), handle(in_handle) {}

  ~FilesystemFileReader() {
    if (handle != impl::INVALID_FILE_HANDLE) {
      fs.Close(handle);
    }
  }

  virtual platform::FilesystemResult<void> Seek(int) override {
    return nonstd::make_unexpected(platform::FilesystemError::Failed);
  }

  virtual platform::FilesystemResult<size_t> Read(char* dst, size_t n) override {
    auto result = fs.Read(handle, dst, n);
    if (result.value == impl::FilesystemResult::OK) {
      return result.bytes_read;
    }
    return nonstd::make_unexpected(platform::FilesystemError::IOError);
  }
};

/**
 * IDirectory / IStorageDirectory implementation backed by IFilesystem at a
 * given absolute base path. Allows the upload thread to read batch files
 * written by EventStorage via the new filesystem abstraction.
 *
 * Used in tests to bridge MockFilesystem (write path) with the upload thread's
 * IDirectory-based read path.
 */
class FilesystemDirectory : public platform::IDirectory {
 public:
  impl::IFilesystem& fs;
  std::string path;

  explicit FilesystemDirectory(impl::IFilesystem& in_fs, std::string in_path)
      : fs(in_fs), path(std::move(in_path)) {}

  virtual platform::FilesystemResult<void> ListFiles(
      std::vector<std::string>& out_names
  ) const override {
    impl::PlatformPath p;
    if (!p.Encode(path.c_str())) {
      return nonstd::make_unexpected(platform::FilesystemError::Failed);
    }
    auto result = fs.ListFiles(p, out_names);
    if (result == impl::FilesystemResult::OK) {
      return {};
    }
    return nonstd::make_unexpected(MapFilesystemError(result));
  }

  virtual platform::FilesystemResult<void> RemoveFile(std::string_view name) override {
    impl::PlatformPath p;
    std::string full = path + "/" + std::string(name);
    if (!p.Encode(full.c_str())) {
      return nonstd::make_unexpected(platform::FilesystemError::Failed);
    }
    auto result = fs.Delete(p);
    if (result == impl::FilesystemResult::OK) {
      return {};
    }
    return nonstd::make_unexpected(MapFilesystemError(result));
  }

  virtual platform::FilesystemResult<std::unique_ptr<platform::IFileReader>>
  OpenForRead(std::string_view name) override {
    impl::PlatformPath p;
    std::string full = path + "/" + std::string(name);
    if (!p.Encode(full.c_str())) {
      return nonstd::make_unexpected(platform::FilesystemError::Failed);
    }
    auto result = fs.OpenForRead(p, false);
    if (result.value == impl::FilesystemResult::OK) {
      return std::make_unique<FilesystemFileReader>(fs, result.handle);
    }
    return nonstd::make_unexpected(MapFilesystemError(result.value));
  }

  virtual platform::FilesystemResult<std::unique_ptr<platform::IDirectory>>
  PrepareSubdirectory(std::string_view name) override {
    return std::make_unique<FilesystemDirectory>(fs, path + "/" + std::string(name));
  }
};

/**
 * IStorageDirectory implementation backed by IFilesystem at a given absolute
 * base path, delegating all operations to FilesystemDirectory.
 */
class FilesystemStorageDirectory : public platform::IStorageDirectory {
  FilesystemDirectory _impl;

 public:
  explicit FilesystemStorageDirectory(impl::IFilesystem& fs, std::string base_path)
      : _impl(fs, std::move(base_path)) {}

  virtual platform::FilesystemResult<void> ListFiles(
      std::vector<std::string>& out_names
  ) const override {
    return _impl.ListFiles(out_names);
  }

  virtual platform::FilesystemResult<void> RemoveFile(std::string_view name) override {
    return _impl.RemoveFile(name);
  }

  virtual platform::FilesystemResult<std::unique_ptr<platform::IFileReader>>
  OpenForRead(std::string_view name) override {
    return _impl.OpenForRead(name);
  }

  virtual platform::FilesystemResult<std::unique_ptr<platform::IDirectory>>
  PrepareSubdirectory(std::string_view name) override {
    return _impl.PrepareSubdirectory(name);
  }
};
