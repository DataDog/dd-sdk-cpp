// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "datadog/impl/assert.hpp"
#include "datadog/impl/storage/filesystem.hpp"

/**
 * Mock filesystem implementation for testing.
 *
 * Provides an in-memory simulation of filesystem operations that matches Windows
 * implementation behavior with permissive share modes and exclusive advisory locks.
 * Thread-safe via mutex-protected shared state. Detects resource leaks by asserting
 * on unclosed handles in destructor.
 */
class MockFilesystemNew : public datadog::impl::IFilesystem {
 public:
  MockFilesystemNew() = default;
  ~MockFilesystemNew() override;

  // Noncopyable, non-movable (due to mutex members)
  MockFilesystemNew(const MockFilesystemNew&) = delete;
  MockFilesystemNew& operator=(const MockFilesystemNew&) = delete;
  MockFilesystemNew(MockFilesystemNew&&) = delete;
  MockFilesystemNew& operator=(MockFilesystemNew&&) = delete;

  // IFilesystem interface implementation
  datadog::impl::FilesystemResult CreateDirectory(
      const datadog::impl::PlatformPath& path
  ) override;

  datadog::impl::FilesystemResult ListFiles(
      const datadog::impl::PlatformPath& path, std::vector<std::string>& out_names
  ) override;

  datadog::impl::FilesystemResult ListSubdirectories(
      const datadog::impl::PlatformPath& path, std::vector<std::string>& out_names
  ) override;

  OpenFileResult OpenForWrite(
      const datadog::impl::PlatformPath& path, bool append, bool hold_advisory_lock
  ) override;

  OpenFileResult OpenForRead(
      const datadog::impl::PlatformPath& path, bool acquire_advisory_lock
  ) override;

  WriteResult Write(
      datadog::impl::PlatformFileHandle file, const char* src, size_t n
  ) override;

  ReadResult Read(datadog::impl::PlatformFileHandle file, char* dst, size_t n) override;

  datadog::impl::FilesystemResult Close(
      datadog::impl::PlatformFileHandle file
  ) override;

  datadog::impl::FilesystemResult Delete(
      const datadog::impl::PlatformPath& path
  ) override;

  datadog::impl::FilesystemResult DeleteDirectory(
      const datadog::impl::PlatformPath& path
  ) override;

  datadog::impl::FilesystemResult Rename(
      const datadog::impl::PlatformPath& src, const datadog::impl::PlatformPath& dst
  ) override;

  // Test helper methods
  void Touch(std::string_view path, std::string_view initial_data = "");
  void Mkdirs(std::string_view path);
  void Corrupt(std::string_view path);
  void SetFail(std::string_view path, bool fail);
  std::string Cat(std::string_view path);
  std::vector<std::string> FindFiles(std::string_view path);
  int GetNumFilesDeleted() const;
  std::vector<int> GetOpenHandles() const;

 private:
  struct MockFileEntry {
    std::string data;
    std::vector<int> open_handles;
    std::optional<int> advisory_lock_holder;
    bool bad = false;
    bool fail = false;
    mutable std::mutex mutex;
  };

  struct MockDirEntry {
    bool bad = false;
    bool fail = false;
  };

  struct HandleInfo {
    std::string path;
    bool is_write;
    bool has_advisory_lock;
    size_t read_offset;
  };

  std::map<std::string, MockFileEntry> files_;
  std::map<std::string, MockDirEntry> dirs_;
  std::map<int, HandleInfo> handles_;
  int next_fd_ = 1;
  int num_files_deleted_ = 0;
  mutable std::mutex mutex_;

  // Helper methods
  static std::string NormalizePath(const datadog::impl::PlatformPath& path);
  static std::string GetParentPath(const std::string& path);
  static std::string GetBasename(const std::string& path);
  bool IsDirectory(const std::string& path);
  bool IsFile(const std::string& path);
};
