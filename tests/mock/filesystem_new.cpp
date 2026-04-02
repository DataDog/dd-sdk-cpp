// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "mock/filesystem_new.hpp"

#include <algorithm>
#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

using namespace datadog::impl;

std::string MockFilesystemNew::NormalizePath(const PlatformPath& path) {
#ifdef _WIN32
  // On Windows, PlatformPath holds a UTF-16 string in a wchar_t buffer: convert to
  // UTF-8 and normalize
  const wchar_t* wstr = path.Get();
  if (!wstr) {
    return {};
  }
  const int num_utf8_bytes = WideCharToMultiByte(
      CP_UTF8, WC_ERR_INVALID_CHARS, wstr, -1, nullptr, 0, nullptr, nullptr
  );
  if (num_utf_8_bytes == 0) {
    return {};
  }
  std::string utf8(size_needed - 1, '\0');
  const int result = WideCharToMultiByte(
      CP_UTF8,
      WC_ERR_INVALID_CHARS,
      wstr,
      -1,
      utf8.data(),
      size_needed,
      nullptr,
      nullptr
  );
  if (result == 0) {
    return {};
  }
  // Normalize to forward slashes
  std::replace(utf8.begin(), utf8.end(), '\\', '/');
  return utf8;
#else
  // On other systems, PlatformPath just holds a pointer to a UTF-8 string: create a
  // copy via std::string
  const char* str = path.Get();
  return str ? std::string(str) : "";
#endif
}

std::string MockFilesystemNew::GetParentPath(const std::string& normalized_path) {
  const size_t pos = normalized_path.find_last_of('/');
  if (pos == std::string::npos) {
    return "";
  }
  return normalized_path.substr(0, pos);
}

std::string MockFilesystemNew::GetBasename(const std::string& normalized_path) {
  const size_t pos = normalized_path.find_last_of('/');
  if (pos == std::string::npos) {
    return normalized_path;
  }
  return normalized_path.substr(pos + 1);
}

FilesystemResult MockFilesystemNew::CreateDirectory(const PlatformPath& path) {
  const std::string normalized_path = NormalizePath(path);
  std::lock_guard lock(_mutex);

  // Report DoesNotExist if parent dir does not exist
  const std::string parent_dir_path = GetParentPath(normalized_path);
  auto found_parent_dir = _dirs.find(parent_dir_path);
  if (found_parent_dir == _dirs.end()) {
    return FilesystemResult::DoesNotExist;
  }
  const MockDirEntry& parent_dir = found_parent_dir->second;

  // Report mock failure if parent dir has non-OK status set
  if (auto status = HasSimulatedFailure(parent_dir, FailureFlags::Mkdir)) {
    return *status;
  }

  // Report AlreadyExists (error) if the target path is already occupied by a file
  if (_files.find(normalized_path) != _files.end()) {
    return FilesystemResult::AlreadyExists;
  }

  // Report AlreadyExistsAsDirectory (fine) if target directory already exists
  if (_dirs.find(normalized_path) != _dirs.end()) {
    return FilesystemResult::AlreadyExistsAsDirectory;
  }

  // Parent directory is valid and target path is not occupied: create a directory entry
  // and return OK
  _dirs[normalized_path] = MockDirEntry{};
  return FilesystemResult::OK;
}

FilesystemResult MockFilesystemNew::ListFiles(
    const PlatformPath& path, std::vector<std::string>& out_names
) {
  const std::string normalized_path = NormalizePath(path);
  std::lock_guard lock(_mutex);

  // Clear output vector
  out_names.clear();

  // Report DoesNotExist if target directory doesn't exist
  auto found_dir = _dirs.find(normalized_path);
  if (found_dir == _dirs.end()) {
    return FilesystemResult::DoesNotExist;
  }
  const MockDirEntry& dir = found_dir->second;

  // Report mock failure if target dir has non-OK status set
  if (auto status = HasSimulatedFailure(dir, FailureFlags::Ls)) {
    return *status;
  }

  // Iterate over our full list of known files, filtering down to only the set of files
  // that are direct children of the target directory
  const std::string prefix = normalized_path.empty() ? "" : (normalized_path + "/");
  const size_t prefix_len = prefix.size();
  for (const auto& [file_path, entry] : _files) {
    const size_t n = file_path.size();
    // Skip files that don't begin with the parent-directory prefix
    if (n <= prefix_len || file_path.find(prefix) != 0) {
      continue;
    }
    // Skip files that are nested deeper than the parent directory
    if (file_path.find('/', prefix_len + 1) != std::string::npos) {
      continue;
    }
    out_names.push_back(file_path.substr(prefix_len));
  }

  // Sort alphabetically for deterministic results
  std::sort(out_names.begin(), out_names.end());

  // We've populated out_names; return OK
  return FilesystemResult::OK;
}

FilesystemResult MockFilesystemNew::ListSubdirectories(
    const PlatformPath& path, std::vector<std::string>& out_names
) {
  const std::string normalized_path = NormalizePath(path);
  std::lock_guard lock(_mutex);

  // Clear output vector
  out_names.clear();

  // Report DoesNotExist if target directory doesn't exist
  auto found_dir = _dirs.find(normalized_path);
  if (found_dir == _dirs.end()) {
    return FilesystemResult::DoesNotExist;
  }
  const MockDirEntry& dir = found_dir->second;

  // Report mock failure if target dir has non-OK status set
  if (auto status = HasSimulatedFailure(dir, FailureFlags::Ls)) {
    return *status;
  }

  // Iterate over our full list of known directories, filtering down to only the set of
  // directories that are direct children of the target directory
  const std::string prefix = normalized_path.empty() ? "" : (normalized_path + "/");
  const size_t prefix_len = prefix.size();
  for (const auto& [dir_path, entry] : _dirs) {
    const size_t n = dir_path.size();
    // Skip files that don't begin with the parent-directory prefix
    if (n <= prefix_len || dir_path.find(prefix) != 0) {
      continue;
    }
    // Skip files that are nested deeper than the parent directory
    if (dir_path.find('/', prefix_len + 1) != std::string::npos) {
      continue;
    }
    out_names.push_back(dir_path.substr(prefix_len));
  }

  // We've populated out_names; return OK
  return FilesystemResult::OK;
}

IFilesystem::OpenFileResult MockFilesystemNew::OpenForWrite(
    const PlatformPath& path, bool append, bool hold_advisory_lock
) {
  std::string normalized_path = NormalizePath(path);
  std::lock_guard lock(_mutex);

  // Report DoesNotExist if parent dir does not exist
  const std::string parent_dir_path = GetParentPath(normalized_path);
  auto found_parent_dir = _dirs.find(parent_dir_path);
  if (found_parent_dir == _dirs.end()) {
    return {FilesystemResult::DoesNotExist, INVALID_FILE_HANDLE};
  }
  const MockDirEntry& parent_dir = found_parent_dir->second;

  // Report mock failure if parent dir has non-OK status set
  if (auto status = HasSimulatedFailure(parent_dir, FailureFlags::Open)) {
    return {*status, INVALID_FILE_HANDLE};
  }

  // Report AlreadyExistsAsDirectory (error) if target path is occupied by a directory
  if (_dirs.find(normalized_path) != _dirs.end()) {
    return {FilesystemResult::AlreadyExistsAsDirectory, INVALID_FILE_HANDLE};
  }

  // Default-construct a MockFileEntry at the given path if we don't already have one
  if (_files.find(normalized_path) == _files.end()) {
    _files.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(normalized_path),
        std::forward_as_tuple()
    );
  }
  MockFileEntry& file = _files[normalized_path];

  // If tests have manually set a non-OK status for this file, fail with that result
  if (auto status = HasSimulatedFailure(file, FailureFlags::Open)) {
    return {*status, INVALID_FILE_HANDLE};
  }

  // Report LockContention if we want to hold an advisory lock but someone already holds
  // such a lock
  if (hold_advisory_lock && file.advisory_lock_holder != INVALID_FILE_HANDLE) {
    return {FilesystemResult::LockContention, INVALID_FILE_HANDLE};
  }

  // OK to open: store the details of a new file handle before returning it
  PlatformFileHandle handle = _next_handle++;

  // If we haven't opened in append mode, truncate the file
  if (!append) {
    file.data.clear();
  }

  // Keep track of handle state within the file entry
  file.open_handles.push_back(handle);
  if (hold_advisory_lock) {
    file.advisory_lock_holder = handle;
  }

  // Keep track of the details of this specific handle
  MockHandleInfo handle_info;
  handle_info.path = normalized_path;
  handle_info.is_write = true;
  handle_info.has_advisory_lock = hold_advisory_lock;
  handle_info.read_offset = 0;
  _handles[handle] = handle_info;

  // Success: the caller now has a valid file handle
  return {FilesystemResult::OK, handle};
}

IFilesystem::OpenFileResult MockFilesystemNew::OpenForRead(
    const PlatformPath& path, bool hold_advisory_lock
) {
  const std::string normalized_path = NormalizePath(path);
  std::lock_guard lock(_mutex);

  // Report mock failure if parent dir has non-OK status set
  const std::string parent_dir_path = GetParentPath(normalized_path);
  auto found_parent_dir = _dirs.find(parent_dir_path);
  if (found_parent_dir != _dirs.end()) {
    const MockDirEntry& parent_dir = found_parent_dir->second;
    if (auto status = HasSimulatedFailure(parent_dir, FailureFlags::Open)) {
      return {*status, INVALID_FILE_HANDLE};
    }
  }

  // Check for a FileEntry, reporting DoesNotExist if not found
  auto found = _files.find(normalized_path);
  if (found == _files.end()) {
    return {FilesystemResult::DoesNotExist, INVALID_FILE_HANDLE};
  }
  MockFileEntry& file = found->second;

  // If tests have manually set a non-OK status for this file, fail with that result
  if (auto status = HasSimulatedFailure(file, FailureFlags::Open)) {
    return {*status, INVALID_FILE_HANDLE};
  }

  // Report LockContention if we want to hold an advisory lock but someone already holds
  // such a lock
  if (hold_advisory_lock && file.advisory_lock_holder != INVALID_FILE_HANDLE) {
    return {FilesystemResult::LockContention, INVALID_FILE_HANDLE};
  }

  // OK to open: store the details of a new file handle before returning it
  PlatformFileHandle handle = _next_handle++;

  // Keep track of handle state within the file entry
  file.open_handles.push_back(handle);
  if (hold_advisory_lock) {
    file.advisory_lock_holder = handle;
  }

  // Keep track of the details of this specific handle
  MockHandleInfo handle_info;
  handle_info.path = normalized_path;
  handle_info.is_write = false;
  handle_info.has_advisory_lock = hold_advisory_lock;
  handle_info.read_offset = 0;
  _handles[handle] = handle_info;

  // Success: the caller now has a valid file handle
  return {FilesystemResult::OK, handle};
}

IFilesystem::WriteResult MockFilesystemNew::Write(
    PlatformFileHandle handle, const char* src, size_t n
) {
  std::lock_guard lock(_mutex);

  // Look up the handle value, reporting UnknownError if no such handle exists
  auto found_handle = _handles.find(handle);
  if (found_handle == _handles.end()) {
    // File I/O with a bad fd/handle would result in EBADF/ERROR_INVALID_HANDLE, which
    // we don't explicitly differentiate
    return {FilesystemResult::UnknownError, 0};
  }
  const MockHandleInfo& handle_info = found_handle->second;

  // Look up the corresponding file entry, reporting UnknownError if we can't find the
  // file associated with a known handle
  auto found_file = _files.find(handle_info.path);
  if (found_file == _files.end()) {
    return {FilesystemResult::UnknownError, 0};
  }
  MockFileEntry& file = found_file->second;

  // If tests have manually set a non-OK status for this file, fail with that result
  if (auto status = HasSimulatedFailure(file, FailureFlags::IO)) {
    return {*status, 0};
  }

  // Append to the stored contents of the file
  file.data.append(src, n);

  // Success: we've written the desired data to the file
  return {FilesystemResult::OK, n};
}

IFilesystem::ReadResult MockFilesystemNew::Read(
    PlatformFileHandle handle, char* dst, size_t n
) {
  std::lock_guard lock(_mutex);

  // Look up the handle value, reporting UnknownError if no such handle exists
  auto found_handle = _handles.find(handle);
  if (found_handle == _handles.end()) {
    return {FilesystemResult::UnknownError, 0};
  }
  MockHandleInfo& handle_info = found_handle->second;

  // Look up the corresponding file entry, reporting UnknownError if we can't find the
  // file associated with a known handle
  auto found_file = _files.find(handle_info.path);
  if (found_file == _files.end()) {
    return {FilesystemResult::UnknownError, 0};
  }
  const MockFileEntry& file = found_file->second;

  // If tests have manually set a non-OK status for this file, fail with that result
  if (auto status = HasSimulatedFailure(file, FailureFlags::IO)) {
    return {*status, 0};
  }

  // Figure out how much data we can read from the file via this handle
  const size_t num_bytes_available =
      (file.data.size() > handle_info.read_offset
           ? file.data.size() - handle_info.read_offset
           : 0);
  const size_t num_bytes_to_read = std::min(n, num_bytes_available);

  // Copy that number of bytes from the file contents to the output buffer, and move the
  // read offset forward accordingly
  if (num_bytes_to_read > 0) {
    std::memcpy(dst, file.data.data() + handle_info.read_offset, num_bytes_to_read);
    handle_info.read_offset += num_bytes_to_read;
  }

  // Success: we've read 0 or more bytes from the file
  return {FilesystemResult::OK, num_bytes_to_read};
}

FilesystemResult MockFilesystemNew::Close(PlatformFileHandle handle) {
  std::lock_guard lock(_mutex);

  // Look up the handle value, reporting UnknownError if no such handle exists
  auto found_handle = _handles.find(handle);
  if (found_handle == _handles.end()) {
    return FilesystemResult::UnknownError;
  }
  const MockHandleInfo& handle_info = found_handle->second;

  // Look up the corresponding file entry, reporting UnknownError if we can't find the
  // file associated with a known handle
  auto found_file = _files.find(handle_info.path);
  if (found_file == _files.end()) {
    return FilesystemResult::UnknownError;
  }
  MockFileEntry& file = found_file->second;

  // If tests have manually set a non-OK status for this file, fail with that result
  if (auto status = HasSimulatedFailure(file, FailureFlags::Close)) {
    return *status;
  }

  // Remove this handle from the list of open handles for this file
  file.open_handles.erase(
      std::remove(file.open_handles.begin(), file.open_handles.end(), handle),
      file.open_handles.end()
  );

  // Release advisory lock if held
  if (handle_info.has_advisory_lock && file.advisory_lock_holder == handle) {
    file.advisory_lock_holder = INVALID_FILE_HANDLE;
  }

  // File state updated to remove reference to handle; now forget about the handle
  _handles.erase(found_handle);

  // Success: file closed
  return FilesystemResult::OK;
}

FilesystemResult MockFilesystemNew::Delete(const PlatformPath& path) {
  const std::string normalized_path = NormalizePath(path);
  std::lock_guard lock(_mutex);

  // Check for a FileEntry, reporting DoesNotExist if not found
  auto found = _files.find(normalized_path);
  if (found == _files.end()) {
    return FilesystemResult::DoesNotExist;
  }
  MockFileEntry& file = found->second;

  // If tests have manually set a non-OK status for this file, fail with that result
  if (auto status = HasSimulatedFailure(file, FailureFlags::Delete)) {
    return *status;
  }

  // If the file has any open handles, fail on deletion, simulating default Windows/NTFS
  // behavior
  if (!file.open_handles.empty()) {
    // Win32 would report ERROR_SHARING_VIOLATION in this case, which we don't
    // explicitly differentiate
    return FilesystemResult::UnknownError;
  }

  // No handles exist: drop the file entry from our lookup
  _files.erase(found);

  // Success: file no longer exists
  return FilesystemResult::OK;
}

FilesystemResult MockFilesystemNew::DeleteDirectory(const PlatformPath& path) {
  const std::string normalized_path = NormalizePath(path);
  std::lock_guard lock(_mutex);

  // Check for a DirEntry, reporting DoesNotExist if not found
  auto found = _dirs.find(normalized_path);
  if (found == _dirs.end()) {
    return FilesystemResult::DoesNotExist;
  }
  MockDirEntry& dir = found->second;

  // If tests have manually set a non-OK status for this dir, fail with that result
  if (auto status = HasSimulatedFailure(dir, FailureFlags::Delete)) {
    return *status;
  }

  // If any files or subdirectories exist beneath this directory, fail with
  // DirectoryNotEmpty
  const std::string prefix = normalized_path + "/";
  const size_t prefix_len = prefix.size();
  for (const auto& [file_path, _] : _files) {
    if (file_path.size() > prefix_len && file_path.find(prefix) == 0) {
      return FilesystemResult::DirectoryNotEmpty;
    }
  }
  for (const auto& [dir_path, _] : _dirs) {
    if (dir_path.size() > prefix_len && dir_path.find(prefix) == 0) {
      return FilesystemResult::DirectoryNotEmpty;
    }
  }

  // Forget about this directory
  _dirs.erase(found);

  // Success: directory no longer exists
  return FilesystemResult::OK;
}

FilesystemResult MockFilesystemNew::Rename(
    const PlatformPath& src, const PlatformPath& dst
) {
  const std::string normalized_src = NormalizePath(src);
  const std::string normalized_dst = NormalizePath(dst);
  std::lock_guard lock(_mutex);

  // Check to see if the source path is occupied by a file or by a directory, returning
  // DoesNotExist if neither
  auto found_src_file = _files.find(normalized_src);
  auto found_src_dir = _dirs.find(normalized_src);
  const bool is_file = found_src_file != _files.end();
  const bool is_dir = found_src_dir != _dirs.end();
  if (!is_file && !is_dir) {
    return FilesystemResult::DoesNotExist;
  }

  // If tests have manually set a non-OK status for this file or dir, fail with that
  // result
  if (is_file) {
    if (auto st = HasSimulatedFailure(found_src_file->second, FailureFlags::Rename)) {
      return *st;
    }
  } else {
    if (auto st = HasSimulatedFailure(found_src_dir->second, FailureFlags::Rename)) {
      return *st;
    }
  }

  // Similarly, if tests have set a non-OK status for the destination directory, fail
  // with that result
  const std::string dst_parent_dir_path = GetParentPath(normalized_dst);
  auto dst_parent_dir = _dirs.find(dst_parent_dir_path);
  if (dst_parent_dir != _dirs.end()) {
    if (auto st = HasSimulatedFailure(dst_parent_dir->second, FailureFlags::Open)) {
      return *st;
    }
  }

  // Report AlreadyExists if the destination path is already occupied
  if (_files.find(normalized_dst) != _files.end() ||
      _dirs.find(normalized_dst) != _dirs.end()) {
    return FilesystemResult::AlreadyExists;
  }

  // Report DoesNotExist if the parent directory of the destination path does not exist
  if (dst_parent_dir == _dirs.end()) {
    return FilesystemResult::DoesNotExist;
  }

  // If src_path refers to a file, proceed with file move
  if (is_file) {
    // If the file has any open handles, fail, simulating default Windows/NTFS behavior
    if (!found_src_file->second.open_handles.empty()) {
      return FilesystemResult::UnknownError;
    }

    // Unlink the std::pair<std::string, MockFileEntry> item from the STL map, mutate
    // its key to index it under the desired destination path, and reinsert it
    auto node = _files.extract(found_src_file);
    node.key() = normalized_dst;
    _files.insert(std::move(node));

    // There are no files handles to fix up, and no child files/directories, so we're
    // done here: file successfully renamed
    return FilesystemResult::OK;
  }

  // Otherwise, src_path refers to a directory: proceed with directory move. Renaming a
  // directory will also change the path of any subdirectories or files that exist
  // beneath it, so we may need to modify multiple file and directory entries.

  // Start by modifying the directory entry itself, which we've already found
  auto node = _dirs.extract(found_src_dir);
  node.key() = normalized_dst;
  _dirs.insert(std::move(node));

  // We now need to modify any subdirectories or files that we've stored at paths
  // beneath the newly-renamed directory
  const std::string old_prefix = normalized_src + "/";
  const std::string new_prefix = normalized_dst + "/";
  for (auto it = _dirs.begin(); it != _dirs.end();) {
    if (it->first.find(old_prefix) == 0) {
      // Advance iterator after extracting current directory entry's node from the map,
      // then mutate key and reinsert
      auto node = _dirs.extract(it++);
      node.key() = new_prefix + node.key().substr(old_prefix.size());
      _dirs.insert(std::move(node));
    } else {
      // Advance directory iterator normally
      it++;
    }
  }
  for (auto it = _files.begin(); it != _files.end();) {
    if (it->first.find(old_prefix) == 0) {
      // When we change the path to a file, we also need to fix up any paths referenced
      // by open file handles, so we can still resolve the same file entry
      for (const PlatformFileHandle handle : it->second.open_handles) {
        auto found_handle = _handles.find(handle);
        if (found_handle != _handles.end() &&
            found_handle->second.path.find(old_prefix) == 0) {
          found_handle->second.path =
              new_prefix + found_handle->second.path.substr(old_prefix.size());
        }
      }

      // Advance iterator after extracting current file entry's node from the map, then
      // mutate key and reinsert
      auto node = _files.extract(it++);
      node.key() = new_prefix + node.key().substr(old_prefix.size());
      _files.insert(std::move(node));
    } else {
      // Advance file iterator normally
      it++;
    }
  }

  // Success: directory rename complete
  return FilesystemResult::OK;
}

void MockFilesystemNew::Mkdirs(std::string_view path) {
  std::string path_str(path);
  std::lock_guard lock(_mutex);

  // Create all parent directories
  size_t pos = 0;
  while ((pos = path_str.find('/', pos)) != std::string::npos) {
    std::string subpath = path_str.substr(0, pos);
    if (!subpath.empty() && _dirs.find(subpath) == _dirs.end()) {
      _dirs.emplace(subpath, MockDirEntry{});
    }
    ++pos;
  }

  // Create the leaf directory entry
  if (_dirs.find(path_str) == _dirs.end()) {
    _dirs.emplace(path_str, MockDirEntry{});
  }
}

void MockFilesystemNew::Touch(std::string_view path, std::string_view initial_data) {
  std::string path_str(path);
  std::lock_guard lock(_mutex);

  // Create all parent directories
  size_t pos = 0;
  while ((pos = path_str.find('/', pos)) != std::string::npos) {
    std::string subpath = path_str.substr(0, pos);
    if (!subpath.empty() && _dirs.find(subpath) == _dirs.end()) {
      _dirs.emplace(subpath, MockDirEntry{});
    }
    ++pos;
  }

  // Create the file entry
  if (_files.find(path_str) == _files.end()) {
    _files.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(path_str),
        std::forward_as_tuple()
    );
  }
  _files[path_str].data = std::string(initial_data);
}

void MockFilesystemNew::SimulateFailure(
    std::string_view path, FilesystemResult status, FailureFlags flags
) {
  std::string path_str(path);
  std::lock_guard lock(_mutex);

  auto file = _files.find(path_str);
  if (file != _files.end()) {
    file->second.status = status;
    file->second.status_flags = flags;
  }

  auto dir = _dirs.find(path_str);
  if (dir != _dirs.end()) {
    dir->second.status = status;
    dir->second.status_flags = flags;
  }
}

void MockFilesystemNew::ClearSimulatedFailure(std::string_view path) {
  SimulateFailure(path, FilesystemResult::OK);
}

void MockFilesystemNew::LockFile(std::string_view path) {
  std::string path_str(path);
  std::lock_guard lock(_mutex);

  auto file = _files.find(path_str);
  if (file != _files.end() &&
      file->second.advisory_lock_holder == INVALID_FILE_HANDLE) {
    file->second.advisory_lock_holder = 8675309;
  }
}

void MockFilesystemNew::UnlockFile(std::string_view path) {
  std::string path_str(path);
  std::lock_guard lock(_mutex);

  auto file = _files.find(path_str);
  if (file != _files.end() && file->second.advisory_lock_holder == 8675309) {
    file->second.advisory_lock_holder = INVALID_FILE_HANDLE;
  }
}

bool MockFilesystemNew::IsDirectory(std::string_view path) {
  std::string path_str(path);
  std::lock_guard lock(_mutex);
  return _dirs.find(path_str) != _dirs.end();
}

bool MockFilesystemNew::IsFile(std::string_view path) {
  std::string path_str(path);
  std::lock_guard lock(_mutex);
  return _files.find(path_str) != _files.end();
}

bool MockFilesystemNew::IsFileLocked(std::string_view path) {
  std::string path_str(path);
  std::lock_guard lock(_mutex);

  auto file = _files.find(path_str);
  if (file != _files.end()) {
    return file->second.advisory_lock_holder != INVALID_FILE_HANDLE;
  }
  return "";
}

std::string MockFilesystemNew::Cat(std::string_view path) {
  std::string path_str(path);
  std::lock_guard lock(_mutex);

  auto file = _files.find(path_str);
  if (file != _files.end()) {
    return file->second.data;
  }
  return "";
}
