// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cinttypes>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "datadog/impl/core/storage/filesystem.hpp"
#include "datadog/impl/core/storage/filesystem_wrapper.hpp"
#include "datadog/impl/types/assert.hpp"

using namespace datadog;

/**
 * Mock filesystem implementation for testing.
 */
class MockFilesystem : public impl::IFilesystem {
 public:
  /**
   * Flags for use with SimulateFailure(), controlling which operations will fail with
   * the given mock FilesystemResult value when attempted on mock files or directories
   * at (or, in some cases, one level beneath) the given path.
   */
  enum class FailureFlags : uint8_t {
    Stat = (1 << 0),    // GetFileSize will fail for the target file
    Mkdir = (1 << 1),   // CreateDirectory will fail with target dir as parent
    Ls = (1 << 2),      // ListFiles and ListSubdirectories will fail in target dir
    Open = (1 << 3),    // OpenForRead and OpenForWrite will fail on file; in target dir
    IO = (1 << 4),      // Read and Write will fail on handles opened for this file
    Close = (1 << 5),   // Close will fail on handles opened for this file
    Rename = (1 << 6),  // Rename and Replace will fail for target file or dir
    Delete = (1 << 7),  // Delete and DeleteDirectory will fail for target file or dir
    All = 0xff
  };

 private:
  // Global mutex used to synchronize access to _dirs, _files, _handles, etc., such that
  // all threads under test see a consistent view of the simulated disk
  mutable std::mutex _mutex;
  uintptr_t _next_handle{1};

  // When non-zero, Read() returns at most this many bytes per call even if more data is
  // available, to allow tests to exercise retry loops in higher-level code
  size_t _max_read_size{0};

  // Maintain a mapping of normalized paths to details of known directories
  struct MockDirEntry {
    // If not OK, all operations directly targeting this directory will fail
    impl::FilesystemResult status;
    // If not All, only specified operations will fail with non-OK status
    FailureFlags status_flags;
  };
  std::map<std::string, MockDirEntry> _dirs;

  // Maintain a mapping of normalized paths to details of known files
  struct MockFileEntry {
    // Contents stored for this file
    std::string data;
    // List of open file handles
    std::vector<impl::PlatformFileHandle> open_handles;
    // File handle that holds lock, or INVALID_FILE_HANDLE if not locked
    impl::PlatformFileHandle advisory_lock_holder{impl::INVALID_FILE_HANDLE};
    // Delete() was called while this file was open; remove it when last handle closed
    bool delete_on_close{false};
    // If not OK, all operations directly targeting this file will fail with this result
    impl::FilesystemResult status{impl::FilesystemResult::OK};
    // If not All, only specified operations will fail with non-OK status
    FailureFlags status_flags{FailureFlags::All};
  };
  std::map<std::string, MockFileEntry> _files;

  // Maintain a lookup with the details of all extant file handles
  struct MockHandleInfo {
    // Path of the associated file, so we can look up MockFileEntry from handle
    std::string path;
    // True if the file was opened for write
    bool is_write;
    // True if the this handle maintains an advisory lock for exclusive access
    bool has_advisory_lock;
    // Position into the file at which the next read via this handle will start
    size_t read_offset;
  };
  std::map<impl::PlatformFileHandle, MockHandleInfo> _handles;

  // IFilesystem uses PlatformPath, which varies by OS; normalize to std::string values
  // with '/' as delimiter in order to generate paths used for lookup keys etc.
  static std::string NormalizePath(const impl::PlatformPath& path);
  static std::string GetParentPath(const std::string& normalized_path);
  static std::string GetBasename(const std::string& normalized_path);

  // Helper for evaluating simulated failure flags for either a file or dir entry
  template <typename T>
  std::optional<impl::FilesystemResult> HasSimulatedFailure(
      const T& entry, FailureFlags op
  ) const {
    if (entry.status == impl::FilesystemResult::OK) {
      return std::nullopt;
    }
    if ((static_cast<uint8_t>(entry.status_flags) & static_cast<uint8_t>(op)) == 0) {
      return std::nullopt;
    }
    return entry.status;
  }

 public:
  // IFilesystem interface
  impl::FilesystemResult CreateDirectory(const impl::PlatformPath& path) override;
  impl::FilesystemResult ListFiles(
      const impl::PlatformPath& path, std::vector<std::string>& out_names
  ) override;
  impl::FilesystemResult ListSubdirectories(
      const impl::PlatformPath& path, std::vector<std::string>& out_names
  ) override;
  OpenFileResult OpenForWrite(
      const impl::PlatformPath& path, bool append, bool hold_advisory_lock
  ) override;
  OpenFileResult OpenForRead(
      const impl::PlatformPath& path, bool hold_advisory_lock
  ) override;
  WriteResult Write(
      impl::PlatformFileHandle handle, const char* src, size_t n
  ) override;
  ReadResult Read(impl::PlatformFileHandle handle, char* dst, size_t n) override;
  impl::FilesystemResult Close(impl::PlatformFileHandle handle) override;
  GetFileSizeResult GetFileSize(const impl::PlatformPath& path) override;
  impl::FilesystemResult Delete(const impl::PlatformPath& path) override;
  impl::FilesystemResult DeleteDirectory(const impl::PlatformPath& path) override;
  impl::FilesystemResult Rename(
      const impl::PlatformPath& src, const impl::PlatformPath& dst
  ) override;
  impl::FilesystemResult ReplaceFile(
      const impl::PlatformPath& src, const impl::PlatformPath& dst
  ) override;

  // Helper allowing tests to succinctly obtain a convenient wrapper interface
  impl::FilesystemWrapper Wrapper() { return impl::FilesystemWrapper(*this); }

  // Limits the number of bytes returned by a single IFilesystem::Read call, even when
  // more data is available. Used to exercise retry loops in higher-level read code.
  // Pass 0 to remove the limit (default).
  void SetMaxReadSize(size_t max) {
    std::lock_guard lock(_mutex);
    _max_read_size = max;
  }

  // Helper functions for initializing mock filesystem state during tests
  void Mkdirs(std::string_view path);
  void Touch(std::string_view path, std::string_view initial_data = "");
  void SimulateFailure(
      std::string_view path,
      impl::FilesystemResult status,
      FailureFlags flags = FailureFlags::All
  );
  void ClearSimulatedFailure(std::string_view path);
  void LockFile(std::string_view path);
  void UnlockFile(std::string_view path);

  // Helper functions for inspecting filesystem state modified by code under test
  std::vector<std::string> Ls(std::string_view path);
  bool IsDirectory(std::string_view path);
  bool IsFile(std::string_view path);
  bool IsFileLocked(std::string_view path);
  std::string Cat(std::string_view path);
};
