// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "mock/filesystem_new.hpp"

#include <algorithm>
#include <cstring>

using namespace datadog::impl;

#ifdef _WIN32
static int handle_to_int(PlatformFileHandle handle) {
  return static_cast<int>(reinterpret_cast<intptr_t>(handle));
}
static PlatformFileHandle int_to_handle(int fd) {
  return reinterpret_cast<PlatformFileHandle>(static_cast<intptr_t>(fd));
}
#else
static int handle_to_int(PlatformFileHandle handle) { return handle; }
static PlatformFileHandle int_to_handle(int fd) { return fd; }
#endif

MockFilesystemNew::~MockFilesystemNew() {
  std::lock_guard lock(mutex_);
  // Silently clear any remaining open handles so that test failures (e.g. from
  // a REQUIRE throwing mid-section) don't trigger a secondary assertion here that
  // would obscure the original failure.
  handles_.clear();
}

std::string MockFilesystemNew::NormalizePath(const PlatformPath& path) {
#ifdef _WIN32
  // On Windows, convert wchar_t* to UTF-8 string
  const wchar_t* wide_path = path.Get();
  if (wide_path == nullptr) {
    return "";
  }
  // Simple conversion - for mock purposes we can use basic ASCII
  std::string result;
  while (*wide_path) {
    result += static_cast<char>(*wide_path);
    ++wide_path;
  }
  // Normalize backslashes to forward slashes
  std::replace(result.begin(), result.end(), '\\', '/');
  return result;
#else
  const char* str = path.Get();
  return str ? std::string(str) : "";
#endif
}

std::string MockFilesystemNew::GetParentPath(const std::string& path) {
  size_t pos = path.find_last_of('/');
  if (pos == std::string::npos) {
    return "";
  }
  return path.substr(0, pos);
}

std::string MockFilesystemNew::GetBasename(const std::string& path) {
  size_t pos = path.find_last_of('/');
  if (pos == std::string::npos) {
    return path;
  }
  return path.substr(pos + 1);
}

bool MockFilesystemNew::IsDirectory(const std::string& path) {
  std::lock_guard lock(mutex_);
  return dirs_.find(path) != dirs_.end();
}

bool MockFilesystemNew::IsFile(const std::string& path) {
  std::lock_guard lock(mutex_);
  return files_.find(path) != files_.end();
}

FilesystemResult MockFilesystemNew::CreateDirectory(const PlatformPath& path) {
  std::string path_str = NormalizePath(path);
  std::lock_guard lock(mutex_);

  // Check if directory already exists
  if (dirs_.find(path_str) != dirs_.end()) {
    return FilesystemResult::AlreadyExistsAsDirectory;
  }

  // Check if a file exists at this path
  if (files_.find(path_str) != files_.end()) {
    return FilesystemResult::AlreadyExists;
  }

  // Check if parent directory exists
  std::string parent = GetParentPath(path_str);
  if (!parent.empty() && dirs_.find(parent) == dirs_.end()) {
    return FilesystemResult::DoesNotExist;
  }

  // Create directory
  dirs_[path_str] = MockDirEntry{};
  return FilesystemResult::OK;
}

FilesystemResult MockFilesystemNew::ListFiles(
    const PlatformPath& path, std::vector<std::string>& out_names
) {
  std::string path_str = NormalizePath(path);
  std::lock_guard lock(mutex_);

  // Clear output
  out_names.clear();

  // Check if directory exists
  auto dir_it = dirs_.find(path_str);
  if (dir_it == dirs_.end()) {
    return FilesystemResult::DoesNotExist;
  }

  // Check if directory is marked as corrupt or failing
  if (dir_it->second.bad || dir_it->second.fail) {
    return FilesystemResult::UnknownError;
  }

  // List all files in this directory
  std::string prefix = path_str.empty() ? "" : path_str + "/";
  for (const auto& [file_path, entry] : files_) {
    // Check if file is in this directory (direct child only)
    if (file_path.size() > prefix.size() &&
        file_path.substr(0, prefix.size()) == prefix) {
      std::string relative = file_path.substr(prefix.size());
      // Make sure it's a direct child (no additional slashes)
      if (relative.find('/') == std::string::npos) {
        out_names.push_back(relative);
      }
    }
  }

  return FilesystemResult::OK;
}

FilesystemResult MockFilesystemNew::ListSubdirectories(
    const PlatformPath& path, std::vector<std::string>& out_names
) {
  std::string path_str = NormalizePath(path);
  std::lock_guard lock(mutex_);

  // Clear output
  out_names.clear();

  // Check if directory exists
  auto dir_it = dirs_.find(path_str);
  if (dir_it == dirs_.end()) {
    return FilesystemResult::DoesNotExist;
  }

  // Check if directory is marked bad
  if (dir_it->second.bad) {
    return FilesystemResult::UnknownError;
  }

  // List all subdirectories in this directory
  std::string prefix = path_str.empty() ? "" : path_str + "/";
  for (const auto& [dir_path, entry] : dirs_) {
    // Skip the directory itself
    if (dir_path == path_str) {
      continue;
    }
    // Check if directory is in this directory (direct child only)
    if (dir_path.size() > prefix.size() &&
        dir_path.substr(0, prefix.size()) == prefix) {
      std::string relative = dir_path.substr(prefix.size());
      // Make sure it's a direct child (no additional slashes)
      if (relative.find('/') == std::string::npos) {
        out_names.push_back(relative);
      }
    }
  }

  return FilesystemResult::OK;
}

MockFilesystemNew::OpenFileResult MockFilesystemNew::OpenForWrite(
    const PlatformPath& path, bool append, bool hold_advisory_lock
) {
  std::string path_str = NormalizePath(path);
  std::lock_guard global_lock(mutex_);

  // Create file entry if it doesn't exist
  if (files_.find(path_str) == files_.end()) {
    files_.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(path_str),
        std::forward_as_tuple()
    );
  }

  MockFileEntry& entry = files_[path_str];
  std::lock_guard file_lock(entry.mutex);

  // Check for advisory lock contention
  if (hold_advisory_lock && entry.advisory_lock_holder.has_value()) {
    return {FilesystemResult::LockContention, INVALID_FILE_HANDLE};
  }

  // Allocate file descriptor
  int fd = next_fd_++;

  // Truncate file if not appending
  if (!append) {
    entry.data.clear();
  }

  // Track handle
  entry.open_handles.push_back(fd);
  if (hold_advisory_lock) {
    entry.advisory_lock_holder = fd;
  }

  HandleInfo handle_info;
  handle_info.path = path_str;
  handle_info.is_write = true;
  handle_info.has_advisory_lock = hold_advisory_lock;
  handle_info.read_offset = 0;
  handles_[fd] = handle_info;

  return {FilesystemResult::OK, int_to_handle(fd)};
}

MockFilesystemNew::OpenFileResult MockFilesystemNew::OpenForRead(
    const PlatformPath& path, bool acquire_advisory_lock
) {
  std::string path_str = NormalizePath(path);
  std::lock_guard global_lock(mutex_);

  // Check if file exists
  auto file_it = files_.find(path_str);
  if (file_it == files_.end()) {
    return {FilesystemResult::DoesNotExist, INVALID_FILE_HANDLE};
  }

  MockFileEntry& entry = file_it->second;
  std::lock_guard file_lock(entry.mutex);

  // If the file is marked as failing, simulate an open failure
  if (entry.fail) {
    return {FilesystemResult::UnknownError, INVALID_FILE_HANDLE};
  }

  // Check for advisory lock contention
  if (acquire_advisory_lock && entry.advisory_lock_holder.has_value()) {
    return {FilesystemResult::LockContention, INVALID_FILE_HANDLE};
  }

  // Allocate file descriptor
  int fd = next_fd_++;

  // Track handle
  entry.open_handles.push_back(fd);
  if (acquire_advisory_lock) {
    entry.advisory_lock_holder = fd;
  }

  HandleInfo handle_info;
  handle_info.path = path_str;
  handle_info.is_write = false;
  handle_info.has_advisory_lock = acquire_advisory_lock;
  handle_info.read_offset = 0;
  handles_[fd] = handle_info;

  return {FilesystemResult::OK, int_to_handle(fd)};
}

MockFilesystemNew::WriteResult MockFilesystemNew::Write(
    PlatformFileHandle file, const char* src, size_t n
) {
  std::lock_guard global_lock(mutex_);

  // Convert handle to int for internal lookup
  int fd = handle_to_int(file);

  // Find handle
  auto handle_it = handles_.find(fd);
  if (handle_it == handles_.end()) {
    return {FilesystemResult::UnknownError, 0};
  }

  const HandleInfo& handle = handle_it->second;
  auto file_it = files_.find(handle.path);
  if (file_it == files_.end()) {
    return {FilesystemResult::UnknownError, 0};
  }

  MockFileEntry& entry = file_it->second;
  std::lock_guard file_lock(entry.mutex);

  // Check error flags
  if (entry.bad) {
    return {FilesystemResult::UnknownError, 0};
  }
  if (entry.fail) {
    return {FilesystemResult::UnknownError, 0};
  }

  // Append data
  entry.data.append(src, n);
  return {FilesystemResult::OK, n};
}

MockFilesystemNew::ReadResult MockFilesystemNew::Read(
    PlatformFileHandle file, char* dst, size_t n
) {
  std::lock_guard global_lock(mutex_);

  // Convert handle to int for internal lookup
  int fd = handle_to_int(file);

  // Find handle
  auto handle_it = handles_.find(fd);
  if (handle_it == handles_.end()) {
    return {FilesystemResult::UnknownError, 0};
  }

  HandleInfo& handle = handle_it->second;
  auto file_it = files_.find(handle.path);
  if (file_it == files_.end()) {
    return {FilesystemResult::UnknownError, 0};
  }

  MockFileEntry& entry = file_it->second;
  std::lock_guard file_lock(entry.mutex);

  // Check error flags: bad (corrupt) maps to UnknownError (IOError in TLV layer),
  // while fail (soft failure) maps to DoesNotExist (ReadFailed in TLV layer)
  if (entry.bad) {
    return {FilesystemResult::UnknownError, 0};
  }
  if (entry.fail) {
    return {FilesystemResult::DoesNotExist, 0};
  }

  // Calculate how much data to read
  size_t available = entry.data.size() > handle.read_offset
                         ? entry.data.size() - handle.read_offset
                         : 0;
  size_t to_read = std::min(n, available);

  // Copy data
  if (to_read > 0) {
    std::memcpy(dst, entry.data.data() + handle.read_offset, to_read);
    handle.read_offset += to_read;
  }

  return {FilesystemResult::OK, to_read};
}

FilesystemResult MockFilesystemNew::Close(PlatformFileHandle file) {
  std::lock_guard global_lock(mutex_);

  // Convert handle to int for internal lookup
  int fd = handle_to_int(file);

  // Find handle
  auto handle_it = handles_.find(fd);
  if (handle_it == handles_.end()) {
    return FilesystemResult::UnknownError;
  }

  const HandleInfo& handle = handle_it->second;
  auto file_it = files_.find(handle.path);
  if (file_it != files_.end()) {
    MockFileEntry& entry = file_it->second;
    std::lock_guard file_lock(entry.mutex);

    // Remove from open handles
    auto& open_handles = entry.open_handles;
    open_handles.erase(
        std::remove(open_handles.begin(), open_handles.end(), fd), open_handles.end()
    );

    // Release advisory lock if held
    if (handle.has_advisory_lock && entry.advisory_lock_holder == fd) {
      entry.advisory_lock_holder.reset();
    }
  }

  // Remove handle
  handles_.erase(handle_it);
  return FilesystemResult::OK;
}

FilesystemResult MockFilesystemNew::Delete(const PlatformPath& path) {
  std::string path_str = NormalizePath(path);
  std::lock_guard global_lock(mutex_);

  // Check if file exists
  auto file_it = files_.find(path_str);
  if (file_it == files_.end()) {
    return FilesystemResult::DoesNotExist;
  }

  MockFileEntry& entry = file_it->second;
  std::lock_guard file_lock(entry.mutex);

  // Check if file has open handles
  if (!entry.open_handles.empty()) {
    return FilesystemResult::UnknownError;
  }

  // Delete file
  files_.erase(file_it);
  num_files_deleted_++;
  return FilesystemResult::OK;
}

FilesystemResult MockFilesystemNew::DeleteDirectory(const PlatformPath& path) {
  std::string path_str = NormalizePath(path);
  std::lock_guard global_lock(mutex_);

  if (dirs_.find(path_str) == dirs_.end()) {
    return FilesystemResult::DoesNotExist;
  }

  // Fail if any files or subdirectories exist beneath this directory
  std::string prefix = path_str + "/";
  for (const auto& [p, _] : files_) {
    if (p.size() > prefix.size() && p.substr(0, prefix.size()) == prefix) {
      return FilesystemResult::DirectoryNotEmpty;
    }
  }
  for (const auto& [d, _] : dirs_) {
    if (d.size() > prefix.size() && d.substr(0, prefix.size()) == prefix) {
      return FilesystemResult::DirectoryNotEmpty;
    }
  }

  dirs_.erase(path_str);
  return FilesystemResult::OK;
}

FilesystemResult MockFilesystemNew::Rename(
    const PlatformPath& src, const PlatformPath& dst
) {
  std::string src_str = NormalizePath(src);
  std::string dst_str = NormalizePath(dst);
  std::lock_guard global_lock(mutex_);

  // Check if source exists (file or directory)
  auto src_file_it = files_.find(src_str);
  auto src_dir_it = dirs_.find(src_str);
  bool is_file = src_file_it != files_.end();
  bool is_dir = src_dir_it != dirs_.end();

  if (!is_file && !is_dir) {
    return FilesystemResult::DoesNotExist;
  }

  // Check if destination exists
  if (files_.find(dst_str) != files_.end() || dirs_.find(dst_str) != dirs_.end()) {
    return FilesystemResult::AlreadyExists;
  }

  if (is_file) {
    // Move file data (can't move mutex, so copy data and recreate entry)
    std::string data_copy;
    bool bad_copy = false;
    bool fail_copy = false;
    {
      std::lock_guard file_lock(src_file_it->second.mutex);
      data_copy = src_file_it->second.data;
      bad_copy = src_file_it->second.bad;
      fail_copy = src_file_it->second.fail;
      // Note: open_handles and advisory_lock_holder are not copied
      // because rename should not be called on open files
    }
    files_.erase(src_file_it);

    // Create new entry at destination
    files_.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(dst_str),
        std::forward_as_tuple()
    );
    files_[dst_str].data = std::move(data_copy);
    files_[dst_str].bad = bad_copy;
    files_[dst_str].fail = fail_copy;

    // Update handles to point to new path
    for (auto& [fd, handle] : handles_) {
      if (handle.path == src_str) {
        handle.path = dst_str;
      }
    }
  } else {
    // Rename directory and all its contents
    // First, rename the directory itself
    dirs_[dst_str] = src_dir_it->second;
    dirs_.erase(src_dir_it);

    // Then, rename all files and subdirectories under it
    std::string src_prefix = src_str + "/";
    std::string dst_prefix = dst_str + "/";

    // Rename all files
    std::vector<std::pair<std::string, std::string>> files_to_rename;
    for (const auto& [path, entry] : files_) {
      if (path.size() > src_prefix.size() &&
          path.substr(0, src_prefix.size()) == src_prefix) {
        std::string new_path = dst_prefix + path.substr(src_prefix.size());
        files_to_rename.emplace_back(path, new_path);
      }
    }
    for (const auto& [old_path, new_path] : files_to_rename) {
      auto& old_entry = files_[old_path];
      std::lock_guard file_lock(old_entry.mutex);

      // Create new entry and copy data (can't move due to mutex)
      files_.emplace(
          std::piecewise_construct,
          std::forward_as_tuple(new_path),
          std::forward_as_tuple()
      );
      files_[new_path].data = old_entry.data;
      files_[new_path].bad = old_entry.bad;
      files_[new_path].fail = old_entry.fail;

      files_.erase(old_path);
    }

    // Rename all subdirectories
    std::vector<std::pair<std::string, std::string>> dirs_to_rename;
    for (const auto& [path, entry] : dirs_) {
      if (path != dst_str && path.size() > src_prefix.size() &&
          path.substr(0, src_prefix.size()) == src_prefix) {
        std::string new_path = dst_prefix + path.substr(src_prefix.size());
        dirs_to_rename.emplace_back(path, new_path);
      }
    }
    for (const auto& [old_path, new_path] : dirs_to_rename) {
      dirs_[new_path] = dirs_[old_path];
      dirs_.erase(old_path);
    }

    // Update handles to point to new paths
    for (auto& [fd, handle] : handles_) {
      if (handle.path.size() >= src_prefix.size() &&
          handle.path.substr(0, src_prefix.size()) == src_prefix) {
        handle.path = dst_prefix + handle.path.substr(src_prefix.size());
      } else if (handle.path == src_str) {
        handle.path = dst_str;
      }
    }
  }

  return FilesystemResult::OK;
}

// Test helper methods

void MockFilesystemNew::Touch(std::string_view path, std::string_view initial_data) {
  std::lock_guard lock(mutex_);
  std::string path_str(path);
  if (files_.find(path_str) == files_.end()) {
    files_.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(path_str),
        std::forward_as_tuple()
    );
  }
  files_[path_str].data = std::string(initial_data);
}

void MockFilesystemNew::Mkdirs(std::string_view path) {
  std::string path_str(path);
  std::lock_guard lock(mutex_);

  // Create all parent directories
  size_t pos = 0;
  while ((pos = path_str.find('/', pos)) != std::string::npos) {
    std::string subpath = path_str.substr(0, pos);
    if (!subpath.empty() && dirs_.find(subpath) == dirs_.end()) {
      dirs_.emplace(subpath, MockDirEntry{});
    }
    ++pos;
  }

  // Create the directory itself
  if (dirs_.find(path_str) == dirs_.end()) {
    dirs_.emplace(path_str, MockDirEntry{});
  }
}

void MockFilesystemNew::Corrupt(std::string_view path) {
  std::string path_str(path);
  std::lock_guard global_lock(mutex_);

  auto file_it = files_.find(path_str);
  if (file_it != files_.end()) {
    std::lock_guard file_lock(file_it->second.mutex);
    file_it->second.bad = true;
  }

  auto dir_it = dirs_.find(path_str);
  if (dir_it != dirs_.end()) {
    dir_it->second.bad = true;
  }
}

void MockFilesystemNew::SetFail(std::string_view path, bool fail) {
  std::string path_str(path);
  std::lock_guard global_lock(mutex_);

  auto file_it = files_.find(path_str);
  if (file_it != files_.end()) {
    std::lock_guard file_lock(file_it->second.mutex);
    file_it->second.fail = fail;
  }

  auto dir_it = dirs_.find(path_str);
  if (dir_it != dirs_.end()) {
    dir_it->second.fail = fail;
  }
}

std::string MockFilesystemNew::Cat(std::string_view path) {
  std::string path_str(path);
  std::lock_guard global_lock(mutex_);

  auto file_it = files_.find(path_str);
  if (file_it != files_.end()) {
    std::lock_guard file_lock(file_it->second.mutex);
    return file_it->second.data;
  }
  return "";
}

std::vector<std::string> MockFilesystemNew::FindFiles(std::string_view path) {
  std::string path_str(path);
  std::vector<std::string> result;
  PlatformPath platform_path;
  if (!platform_path.Encode(path_str.c_str())) {
    return result;
  }
  ListFiles(platform_path, result);
  return result;
}

int MockFilesystemNew::GetNumFilesDeleted() const {
  std::lock_guard lock(mutex_);
  return num_files_deleted_;
}

std::vector<int> MockFilesystemNew::GetOpenHandles() const {
  std::lock_guard lock(mutex_);
  std::vector<int> result;
  for (const auto& [fd, info] : handles_) {
    result.push_back(fd);
  }
  return result;
}
