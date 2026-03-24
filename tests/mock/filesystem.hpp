// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "datadog/impl/platform/filesystem.hpp"

using namespace datadog;

/**
 * Mock data structure used to simulate a file entry. Our code accesses the filesystem
 * from separate threads concurrently, so we need to simulate actual reads and writes to
 * disk.
 *
 * POSIX systems are more permissive that Windows w.r.t. file locking: POSIX allows
 * multiple open handles for the same file by default, whereas Windows defaults to
 * exclusively locking files (only one handle may be open at a time) unless told
 * otherwise via FILE_SHARE_READ / FILE_SHARE_WRITE. Since the default std::filesystem
 * implementation doesn't have access to these platform-specific file open modes, we're
 * subject to the worst-case behavior for now, and so our mocking code simulates the
 * Windows behavior for portability.
 */
struct MockFileEntry {
  std::string data;  // Current file contents, mutated by writers
  int reader_fd{0};  // If nonzero, file is currently open for read with this fd
  int writer_fd{0};  // If nonzero, file is currently open for write with this fd
  bool bad{false};   // If set, any read or write will result in an I/O error
  bool fail{false};  // If set, any read or write will fail

  std::mutex mutex;  // Synchronizes reads/writes to the same file across threads

  MockFileEntry(std::string_view in_data) : data(in_data) {}
};

/**
 * Mock implementation of IFileReader.
 *
 * Simulates fopen('rb') on ctor, fseek()/fread() on demand, fclose() on dtor.
 */
class MockFileReader : public platform::IFileReader {
 public:
  std::shared_ptr<MockFileEntry> f;
  int fd;
  size_t read_offset;

  explicit MockFileReader(std::shared_ptr<MockFileEntry> in_f, int in_fd)
      : f(in_f), fd(in_fd), read_offset(0) {}

  ~MockFileReader() {
    std::lock_guard lock(f->mutex);

    // Simulate fclose, iff we had the file open
    if (f->reader_fd == fd) {
      f->reader_fd = 0;
    }
  }

  virtual platform::FilesystemResult<void> Seek(int offset) override {
    std::lock_guard lock(f->mutex);

    // If we don't have exclusive access to the file, fail
    if (f->reader_fd != fd || f->writer_fd != 0) {
      return nonstd::make_unexpected(platform::FilesystemError::Failed);
    }

    // If file is flagged bad, simulate I/O error
    if (f->bad) {
      return nonstd::make_unexpected(platform::FilesystemError::IOError);
    }

    // If file is flagged fail, simulate problem with file handle
    if (f->fail) {
      return nonstd::make_unexpected(platform::FilesystemError::Failed);
    }

    // Check if seeking backward past start of file
    if (offset < 0 && static_cast<size_t>(-offset) > read_offset) {
      return nonstd::make_unexpected(platform::FilesystemError::Failed);
    }

    // Increment our read offset
    read_offset = static_cast<size_t>(static_cast<int>(read_offset) + offset);

    return {};
  }

  virtual platform::FilesystemResult<size_t> Read(char* dst, size_t n) override {
    std::lock_guard lock(f->mutex);

    // If we don't have exclusive access to the file, fail
    if (f->reader_fd != fd || f->writer_fd != 0) {
      return nonstd::make_unexpected(platform::FilesystemError::Failed);
    }

    // If file is flagged bad, simulate I/O error
    if (f->bad) {
      return nonstd::make_unexpected(platform::FilesystemError::IOError);
    }

    // If file is flagged fail, simulate problem with file handle
    if (f->fail) {
      return nonstd::make_unexpected(platform::FilesystemError::Failed);
    }

    // Otherwise, prepare to read from the file
    const std::string& s = f->data;
    const char* read_ptr = s.data() + read_offset;
    const char* end = s.data() + s.size();

    // If already at EOF before read, read nothing and return
    if (read_ptr >= end) {
      return 0;
    }

    // Read up to N bytes, bounded by remaining size
    const size_t max_bytes_to_read = end - read_ptr;
    const size_t num_bytes_read = std::min(n, max_bytes_to_read);
    std::memcpy(dst, read_ptr, num_bytes_read);

    // Increment our read offset
    read_offset += num_bytes_read;

    // Return the number of bytes actually read
    return num_bytes_read;
  }
};

struct MockDirEntry {
  bool bad{false};   // If set, any directory operation will result in an I/O error
  bool fail{false};  // If set, any directory operation will fail

  std::mutex mutex;
};

struct MockFilesystem {
  int next_fd{1};

  // Keep underlying filesystem state in memory
  std::unordered_map<std::filesystem::path, std::shared_ptr<MockFileEntry>> files;
  std::unordered_map<std::filesystem::path, std::shared_ptr<MockDirEntry>> dirs;

  // Keep track of how many files we deleted
  size_t num_files_deleted{0};

  // Synchronize access to the above (separate from file-level synchronization)
  mutable std::mutex mutex;

  /**
   * Creates directory entries for all path components in relpath. For example,
   * "foo/bar/baz" creates entries for "foo", "foo/bar", and "foo/bar/baz".
   */
  void Mkdirs(std::filesystem::path relpath) {
    if (relpath.empty()) {
      return;
    }

    std::scoped_lock lock(mutex);

    std::filesystem::path current_path;
    for (const auto& component : relpath) {
      current_path /= component;
      if (dirs.find(current_path) == dirs.end()) {
        dirs[current_path] = std::make_shared<MockDirEntry>();
      }
    }
  }

  /**
   * Ensures that a file exists at the given path, creating it if it doesn't exist.
   */
  void Touch(std::filesystem::path relpath, std::string_view data = "") {
    std::scoped_lock lock(mutex);

    if (files.find(relpath) == files.end()) {
      files[relpath] = std::make_shared<MockFileEntry>(data);
    }
  }

  /**
   * Handles IDirectory::ListFiles given the relevant directory path.
   */
  platform::FilesystemResult<void> HandleListFiles(
      std::filesystem::path relpath, std::vector<std::string>& out_names
  ) const {
    std::scoped_lock lock(mutex);

    // Simulate error if directory is flagged bad or fail
    auto dir = dirs.find(relpath);
    if (dir != dirs.end()) {
      std::scoped_lock dir_lock(dir->second->mutex);

      if (dir->second->bad) {
        return nonstd::make_unexpected(platform::FilesystemError::IOError);
      }

      if (dir->second->fail) {
        return nonstd::make_unexpected(platform::FilesystemError::Failed);
      }
    }

    // Populate out_names with the name of every file entry whose parent is the given
    // directory
    for (const auto& [file_path, file] : files) {
      if (file_path.parent_path() == relpath) {
        out_names.push_back(file_path.filename().generic_string());
      }
    }
    return {};
  }

  /**
   * Handles IDirectory::RemoveFile given the relevant file path.
   */
  platform::FilesystemResult<void> HandleRemoveFile(std::filesystem::path relpath) {
    // Get file reference while holding filesystem mutex
    std::shared_ptr<MockFileEntry> file;
    {
      std::scoped_lock lock(mutex);

      // Check for an existing file entry, propagating IOError from bad directory
      auto file_result = GetFileEntry(relpath);
      if (!file_result.has_value()) {
        return nonstd::make_unexpected(file_result.error());
      }

      // If FileEntry is null, file does not exist
      file = *file_result;
      if (!file) {
        return nonstd::make_unexpected(platform::FilesystemError::DoesNotExist);
      }
    }

    // Acquire file mutex separately to avoid holding both filesystem and file mutex
    // simultaneously, which can create complex deadlock scenarios
    std::unique_lock file_lock(file->mutex);

    // If file is flagged bad, fail with I/O error
    if (file->bad) {
      return nonstd::make_unexpected(platform::FilesystemError::IOError);
    }

    // If file is flagged fail, fail
    if (file->fail) {
      return nonstd::make_unexpected(platform::FilesystemError::Failed);
    }

    // If any handles to the file are open, fail
    if (file->reader_fd != 0 || file->writer_fd != 0) {
      return nonstd::make_unexpected(platform::FilesystemError::Failed);
    }

    // Release file mutex before reacquiring filesystem mutex
    file_lock.unlock();

    // Remove the file entry from the filesystem map
    {
      std::scoped_lock lock(mutex);
      files.erase(relpath);
      num_files_deleted++;
    }

    return {};
  }

  /**
   * Handles IDirectory::MoveFile given the relevant filepath.
   */
  platform::FilesystemResult<void> HandleMoveFile(
      std::filesystem::path src_relpath, const std::filesystem::path& dst_dir_relpath
  ) {
    // Get source file reference and prepare destination while holding filesystem mutex
    std::shared_ptr<MockFileEntry> src_file;
    std::filesystem::path dst_file_relpath;
    {
      std::scoped_lock lock(mutex);

      // Check for an existing source file entry, propagating IOError from bad directory
      auto src_file_result = GetFileEntry(src_relpath);
      if (!src_file_result.has_value()) {
        return nonstd::make_unexpected(src_file_result.error());
      }

      // If src FileEntry is null, src file does not exist
      src_file = *src_file_result;
      if (!src_file) {
        return nonstd::make_unexpected(platform::FilesystemError::DoesNotExist);
      }

      // If the destination directory does not exist, implicitly create it
      std::filesystem::path current_path;
      for (const auto& component : dst_dir_relpath) {
        current_path /= component;
        if (dirs.find(current_path) == dirs.end()) {
          dirs[current_path] = std::make_shared<MockDirEntry>();
        }
      }

      // Check for an existing destination file entry, propagating IOError from bad dir
      dst_file_relpath = dst_dir_relpath / src_relpath.filename();
      auto dst_file_result = GetFileEntry(dst_file_relpath);
      if (!dst_file_result.has_value()) {
        return nonstd::make_unexpected(dst_file_result.error());
      }

      // If dst FileEntry is not null, dst file already exists and the move should fail
      if (dst_file_result.value()) {
        return nonstd::make_unexpected(platform::FilesystemError::AlreadyExists);
      }
    }

    // Acquire file mutex separately to avoid holding both filesystem and file mutex
    // simultaneously, which can create complex deadlock scenarios
    std::unique_lock src_file_lock(src_file->mutex);

    // If src file is flagged bad, fail with I/O error
    if (src_file->bad) {
      return nonstd::make_unexpected(platform::FilesystemError::IOError);
    }

    // If src file is flagged fail, fail
    if (src_file->fail) {
      return nonstd::make_unexpected(platform::FilesystemError::Failed);
    }

    // If any handles to the src file are open, fail
    if (src_file->reader_fd != 0 || src_file->writer_fd != 0) {
      return nonstd::make_unexpected(platform::FilesystemError::Failed);
    }

    // Copy the file data before releasing the file lock
    std::string file_data = src_file->data;
    src_file_lock.unlock();

    // Update filesystem map with copied data
    {
      std::scoped_lock lock(mutex);
      files[dst_file_relpath] = std::make_shared<MockFileEntry>(file_data);
      files.erase(src_relpath);
      num_files_deleted++;
    }

    return {};
  }

  /**
   * Handles IDirectory::OpenForRead given the relevant directory path.
   */
  platform::FilesystemResult<std::unique_ptr<platform::IFileReader>> HandleOpenForRead(
      std::filesystem::path relpath
  ) {
    // Acquire filesystem mutex
    std::scoped_lock lock(mutex);

    // Check for an existing file entry, propagating IOError from bad directory
    auto file_result = GetFileEntry(relpath);
    if (!file_result.has_value()) {
      return nonstd::make_unexpected(file_result.error());
    }

    // If FileEntry is null, file does not exist
    auto file = *file_result;
    if (!file) {
      return nonstd::make_unexpected(platform::FilesystemError::DoesNotExist);
    }

    // Acquire mutex and check file state before initializing a MockFileReader
    int fd = -1;
    {
      std::lock_guard file_lock(file->mutex);

      // Ensure exclusivity (Windows-style locking)
      if (file->reader_fd != 0 || file->writer_fd != 0) {
        return nonstd::make_unexpected(platform::FilesystemError::Failed);
      }

      // If bad flag is set for file, return IOError
      if (file->bad) {
        return nonstd::make_unexpected(platform::FilesystemError::IOError);
      }

      // If fail flag is set for file, return Failed
      if (file->fail) {
        return nonstd::make_unexpected(platform::FilesystemError::Failed);
      }

      // Assign the file handle to the FileEntry while we have it locked; MockFileReader
      // will take ownership when constructed below
      fd = next_fd++;
      file->reader_fd = fd;
    }

    // Create a reader and give it a shared_ptr to the FileEntry
    return std::make_unique<MockFileReader>(file, fd);
  }

 private:
  /**
   * Retrieves the MockFileEntry at the given path, or nullptr if no such file is known.
   * Returns unexpected IOError if the parent directory entry has its bad flag set;
   * otherwise returns no error.
   *
   * @note MockFilesystem::mutex MUST already be held by the caller.
   */
  platform::FilesystemResult<std::shared_ptr<MockFileEntry>> GetFileEntry(
      std::filesystem::path relpath
  ) {
    // Simulate error if directory is flagged bad or fail
    auto dir = dirs.find(relpath.parent_path());
    if (dir != dirs.end()) {
      std::scoped_lock dir_lock(dir->second->mutex);

      if (dir->second->bad) {
        return nonstd::make_unexpected(platform::FilesystemError::IOError);
      }

      if (dir->second->fail) {
        return nonstd::make_unexpected(platform::FilesystemError::Failed);
      }
    }

    // Check for existing file
    auto file = files.find(relpath);
    if (file != files.end()) {
      return file->second;
    }

    // Don't signal DoesNotExist; return nullptr for internal usage
    return nullptr;
  }
};

/**
 * Mock implementation of IDirectory. Uses a reference to the MockFilesystem for
 * visibility into the subset of the directory tree that it manages.
 */
class MockDirectory : public platform::IDirectory {
 public:
  MockFilesystem& fs;
  std::filesystem::path relpath;

  MockDirectory(MockFilesystem& in_fs, std::filesystem::path in_relpath)
      : fs(in_fs), relpath(in_relpath) {}

  virtual platform::FilesystemResult<void> ListFiles(
      std::vector<std::string>& out_names
  ) const override {
    return fs.HandleListFiles(relpath, out_names);
  }

  virtual platform::FilesystemResult<void> RemoveFile(std::string_view name) override {
    return fs.HandleRemoveFile(relpath / name);
  }

  virtual platform::FilesystemResult<std::unique_ptr<platform::IFileReader>>
  OpenForRead(std::string_view name) override {
    return fs.HandleOpenForRead(relpath / name);
  }

  virtual platform::FilesystemResult<std::unique_ptr<platform::IDirectory>>
  PrepareSubdirectory(std::string_view name) override {
    return std::make_unique<MockDirectory>(fs, relpath / name);
  }
};

/**
 * Mock implementation of IStorageDirectory. Wraps a MockFilesystem and simulates the
 * interface to the root event storage directory.
 */
class MockStorageDirectory : public platform::IStorageDirectory {
 public:
  MockFilesystem fs;

  MockStorageDirectory() {}

  // IDirectory interface implementation - delegate to MockFilesystem
  virtual platform::FilesystemResult<void> ListFiles(
      std::vector<std::string>& out_names
  ) const override {
    return fs.HandleListFiles("", out_names);
  }

  virtual platform::FilesystemResult<void> RemoveFile(std::string_view name) override {
    return fs.HandleRemoveFile(name);
  }

  virtual platform::FilesystemResult<std::unique_ptr<platform::IFileReader>>
  OpenForRead(std::string_view name) override {
    return fs.HandleOpenForRead(name);
  }

  virtual platform::FilesystemResult<std::unique_ptr<platform::IDirectory>>
  PrepareSubdirectory(std::string_view name) override {
    return std::make_unique<MockDirectory>(fs, name);
  }

  /**
   * Called during test setup to initialize the mock filesystem with files.
   */
  void WithExistingFile(std::string_view relpath, std::string_view data) {
    const std::filesystem::path path{relpath};
    fs.Mkdirs(path.parent_path());
    fs.Touch(path, data);
  }

  /**
   * Sets the bad flag for any file and/or directory at the given path, ensuring that
   * all subsequent operations on that file and/or directory will result in an I/O
   * error.
   */
  void Corrupt(std::string_view relpath) {
    auto file = fs.files.find(relpath);
    if (file != fs.files.end()) {
      std::scoped_lock lock(file->second->mutex);
      file->second->bad = true;
    }
    auto dir = fs.dirs.find(relpath);
    if (dir != fs.dirs.end()) {
      std::scoped_lock lock(dir->second->mutex);
      dir->second->bad = true;
    }
  }

  /**
   * Sets or clears the 'fail' flag for any file and/or directory at the given path. If
   * the flag is set, all subsequent operations targeting that file and/or directory
   * will fail until the flag is cleared.
   */
  void SetFail(std::string_view relpath, bool fail) {
    auto file = fs.files.find(relpath);
    if (file != fs.files.end()) {
      std::scoped_lock lock(file->second->mutex);
      file->second->fail = fail;
    }
    auto dir = fs.dirs.find(relpath);
    if (dir != fs.dirs.end()) {
      std::scoped_lock lock(dir->second->mutex);
      dir->second->fail = fail;
    }
  }

  /**
   * Given the path to a directory, collects the full set of files that have been
   * written to that directory and returns a vector containing their full paths. For
   * examination of filesystem state after a test; does not exhibit simulated filesystem
   * behavior or acquire locks.
   */
  std::vector<std::string> FindFiles(std::filesystem::path relpath) {
    std::vector<std::string> result;
    for (const auto& file : fs.files) {
      if (file.first.parent_path() == relpath) {
        result.push_back(file.first.generic_string());
      }
    }
    return result;
  }

  /**
   * Retrieves the current contents of the file at the given path, if any. This function
   * is intended for the validation of filesystem state at the end of a test; it does
   * not acquire file-level mutexes, read or modify file descriptors, or respect
   * bad/fail flags.
   */
  std::optional<std::string> Cat(std::string_view relpath) {
    auto file = fs.files.find(relpath);
    if (file != fs.files.end()) {
      return file->second->data;
    }
    return std::nullopt;
  }

  /**
   * Returns the total number of files that have been successfully deleted.
   */
  size_t GetNumFilesDeleted() const { return fs.num_files_deleted; }
};
