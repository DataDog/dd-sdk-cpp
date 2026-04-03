// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/storage/filesystem_wrapper.hpp"

#include <utility>

#include "datadog/impl/assert.hpp"

namespace datadog::impl {

// === File ===

File::File(IFilesystem& in_fs, PlatformFileHandle in_handle)
    : _fs(in_fs), _handle(in_handle) {}

File::~File() { Close(); }

File::File(File&& other) noexcept
    : _fs(other._fs), _handle(std::exchange(other._handle, INVALID_FILE_HANDLE)) {}

IFilesystem::WriteResult File::Write(const char* src, size_t n) {
  return _fs.Write(_handle, src, n);
}

IFilesystem::ReadResult File::Read(char* dst, size_t n) {
  return _fs.Read(_handle, dst, n);
}

FilesystemResult File::Close() {
  if (_handle != INVALID_FILE_HANDLE) {
    const FilesystemResult res = _fs.Close(_handle);
    if (res == FilesystemResult::OK) {
      _handle = INVALID_FILE_HANDLE;
    }
    return res;
  }
  // Silently ignore double-close
  return FilesystemResult::OK;
}

// === FilesystemWrapper ===

FilesystemWrapper::FilesystemWrapper(IFilesystem& in_fs) : _fs(in_fs) {}

FilesystemResult FilesystemWrapper::CreateDirectory(const char* path) {
  if (!_path.Encode(path)) {
    return FilesystemResult::PathEncodingFailed;
  }
  return _fs.CreateDirectory(_path);
}

FilesystemResult FilesystemWrapper::ListFiles(
    const char* path, std::vector<std::string>& out_names
) {
  if (!_path.Encode(path)) {
    return FilesystemResult::PathEncodingFailed;
  }
  return _fs.ListFiles(_path, out_names);
}

FilesystemResult FilesystemWrapper::ListSubdirectories(
    const char* path, std::vector<std::string>& out_names
) {
  if (!_path.Encode(path)) {
    return FilesystemResult::PathEncodingFailed;
  }
  return _fs.ListSubdirectories(_path, out_names);
}

FilesystemWrapper::OpenFileResult FilesystemWrapper::OpenForWrite(
    const char* path, bool append, bool hold_advisory_lock
) {
  if (!_path.Encode(path)) {
    return {FilesystemResult::PathEncodingFailed, File{_fs, INVALID_FILE_HANDLE}};
  }
  auto res = _fs.OpenForWrite(_path, append, hold_advisory_lock);
  DATADOG_ASSERT(
      (res.handle != INVALID_FILE_HANDLE) == (res.value == FilesystemResult::OK),
      "filesystem must return valid handle on OK result; invalid handle otherwise"
  );
  return {res.value, File{_fs, res.handle}};
}

FilesystemWrapper::OpenFileResult FilesystemWrapper::OpenForRead(
    const char* path, bool hold_advisory_lock
) {
  if (!_path.Encode(path)) {
    return {FilesystemResult::PathEncodingFailed, File{_fs, INVALID_FILE_HANDLE}};
  }
  auto res = _fs.OpenForRead(_path, hold_advisory_lock);
  DATADOG_ASSERT(
      (res.handle != INVALID_FILE_HANDLE) == (res.value == FilesystemResult::OK),
      "filesystem must return valid handle on OK result; invalid handle otherwise"
  );
  return {res.value, File{_fs, res.handle}};
}

FilesystemResult FilesystemWrapper::Delete(const char* path) {
  if (!_path.Encode(path)) {
    return FilesystemResult::PathEncodingFailed;
  }
  return _fs.Delete(_path);
}

FilesystemResult FilesystemWrapper::DeleteDirectory(const char* path) {
  if (!_path.Encode(path)) {
    return FilesystemResult::PathEncodingFailed;
  }
  return _fs.DeleteDirectory(_path);
}

FilesystemResult FilesystemWrapper::Rename(const char* src, const char* dst) {
  // Rename operates on two paths at once: on platforms that require PlatformPath
  // encoding, use a bit of extra stack space to encode dst
  PlatformPath& src_path = _path;
  PlatformPath dst_path;
  if (!src_path.Encode(src)) {
    return FilesystemResult::PathEncodingFailed;
  }
  if (!dst_path.Encode(dst)) {
    return FilesystemResult::PathEncodingFailed;
  }
  return _fs.Rename(src_path, dst_path);
}

}  // namespace datadog::impl
