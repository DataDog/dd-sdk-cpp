// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <memory>
#include <string>

#include "datadog/impl/storage/filesystem.hpp"

namespace datadog::impl {

static FilesystemResult map_error(DWORD error) {
  switch (error) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
      return FilesystemResult::DoesNotExist;
    case ERROR_ALREADY_EXISTS:
      return FilesystemResult::AlreadyExists;
    case ERROR_DIR_NOT_EMPTY:
      return FilesystemResult::DirectoryNotEmpty;
    case ERROR_ACCESS_DENIED:
      return FilesystemResult::PermissionDenied;
    case ERROR_WRITE_PROTECT:
      return FilesystemResult::ReadOnlyFilesystem;
    case ERROR_DISK_FULL:
    case ERROR_HANDLE_DISK_FULL:
      return FilesystemResult::OutOfSpace;
    case ERROR_FILENAME_EXCED_RANGE:
    case ERROR_BAD_PATHNAME:
      return FilesystemResult::PathTooLong;
    case ERROR_INVALID_NAME:
    case ERROR_BAD_NET_NAME:
      return FilesystemResult::InvalidName;
    case ERROR_LOCK_VIOLATION:
      return FilesystemResult::LockContention;
    default:
      return FilesystemResult::UnknownError;
  }
}

static std::string wide_to_utf8(const std::wstring& wide) {
  if (wide.empty()) {
    return std::string();
  }

  // Calculate required buffer size
  const int size_needed = WideCharToMultiByte(
      CP_UTF8,
      0,
      wide.c_str(),
      static_cast<int>(wide.size()),
      nullptr,
      0,
      nullptr,
      nullptr
  );
  if (size_needed <= 0) {
    return std::string();
  }

  // Convert to UTF-8 string
  std::string utf8(static_cast<size_t>(size_needed), '\0');
  WideCharToMultiByte(
      CP_UTF8,
      0,
      wide.c_str(),
      static_cast<int>(wide.size()),
      &utf8[0],
      size_needed,
      nullptr,
      nullptr
  );
  return utf8;
}

class WindowsFilesystem final : public IFilesystem {
 public:
  FilesystemResult CreateDirectory(const PlatformPath& path) override {
    // Attempt to create directory with default security attributes
    const BOOL result = CreateDirectoryW(path.Get(), nullptr);
    if (result != 0) {
      return FilesystemResult::OK;
    }

    // Creation failed: if a file or directory already exists at the target path, use
    // GetFileAttributesW() to distinguish file vs directory
    const DWORD error = GetLastError();
    if (error == ERROR_ALREADY_EXISTS) {
      const DWORD attrs = GetFileAttributesW(path.Get());
      if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        return FilesystemResult::AlreadyExistsAsDirectory;
      }
    }
    return map_error(error);
  }

  FilesystemResult ListFiles(
      const PlatformPath& path, std::vector<std::string>& out_names
  ) override {
    // Clear output vector
    out_names.clear();

    // Copy our target directory path to a buffer that will hold our search path
    wchar_t search_pattern[MAX_STORAGE_PATH_SIZE + 2];
    const wchar_t* path_str = path.Get();
    size_t len = wcslen(path_str);
    wcscpy_s(search_pattern, MAX_STORAGE_PATH_SIZE + 2, path_str);

    // Append '\' if not already present, then '*', then null-terminate
    if (len > 0 && search_pattern[len - 1] != L'\\') {
      search_pattern[len++] = L'\\';
    }
    search_pattern[len++] = L'*';
    search_pattern[len] = L'\0';

    // Use FindFirstFileW/FindNextFileW to iterate over the contents of the directory
    WIN32_FIND_DATAW find_data;
    HANDLE find_handle = FindFirstFileW(search_pattern, &find_data);
    if (find_handle == INVALID_HANDLE_VALUE) {
      return map_error(GetLastError());
    }
    do {
      // Skip directories - we only want regular files
      if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        // Convert wide filename to UTF-8 and add to output vector
        std::string filename = wide_to_utf8(find_data.cFileName);
        if (!filename.empty()) {
          out_names.push_back(std::move(filename));
        }
      }
    } while (FindNextFileW(find_handle, &find_data) != 0);

    // Check if we exited the loop due to error or end of directory
    const DWORD error = GetLastError();
    FindClose(find_handle);
    return error == ERROR_NO_MORE_FILES ? FilesystemResult::OK : map_error(error);
  }

  FilesystemResult ListSubdirectories(
      const PlatformPath& path, std::vector<std::string>& out_names
  ) override {
    // Clear output vector
    out_names.clear();

    // Copy our target directory path to a buffer that will hold our search path
    wchar_t search_pattern[MAX_STORAGE_PATH_SIZE + 2];
    const wchar_t* path_str = path.Get();
    size_t len = wcslen(path_str);
    wcscpy_s(search_pattern, MAX_STORAGE_PATH_SIZE + 2, path_str);

    // Append '\' if not already present, then '*', then null-terminate
    if (len > 0 && search_pattern[len - 1] != L'\\') {
      search_pattern[len++] = L'\\';
    }
    search_pattern[len++] = L'*';
    search_pattern[len] = L'\0';

    // Use FindFirstFileW/FindNextFileW to iterate over the contents of the directory
    WIN32_FIND_DATAW find_data;
    HANDLE find_handle = FindFirstFileW(search_pattern, &find_data);
    if (find_handle == INVALID_HANDLE_VALUE) {
      return map_error(GetLastError());
    }
    do {
      // Only include directories, excluding "." and ".."
      if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        if (wcscmp(find_data.cFileName, L".") != 0 &&
            wcscmp(find_data.cFileName, L"..") != 0) {
          std::string dirname = wide_to_utf8(find_data.cFileName);
          if (!dirname.empty()) {
            out_names.push_back(std::move(dirname));
          }
        }
      }
    } while (FindNextFileW(find_handle, &find_data) != 0);

    // Check if we exited the loop due to error or end of directory
    const DWORD error = GetLastError();
    FindClose(find_handle);
    return error == ERROR_NO_MORE_FILES ? FilesystemResult::OK : map_error(error);
  }

  OpenFileResult OpenForWrite(
      const PlatformPath& path, bool append, bool hold_advisory_lock
  ) override {
    // OPEN_ALWAYS appends if files already exists, creating it if it doesn't.
    // CREATE_ALWAYS replaces the file if it already exists, creating it if it doesn't.
    const DWORD creation_disposition = append ? OPEN_ALWAYS : CREATE_ALWAYS;

    // FILE_APPEND_DATA restricts writes to end of file; GENERIC_WRITE allows arbitrary
    // positioning
    const DWORD desired_access = append ? FILE_APPEND_DATA : GENERIC_WRITE;

    // Open file with share mode, allowing other processes to read from and write to the
    // file while we have it open. We rely on cooperative advisory locks, not Windows
    // share modes, for file locking.
    HANDLE handle = CreateFileW(
        path.Get(),
        desired_access,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        creation_disposition,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (handle == INVALID_HANDLE_VALUE) {
      return {map_error(GetLastError()), INVALID_FILE_HANDLE};
    }

    // If advisory lock requested, attempt non-blocking exclusive lock
    if (hold_advisory_lock) {
      OVERLAPPED overlapped = {};  // Lock from byte 0
      const BOOL lock_result = LockFileEx(
          handle,
          LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
          0,         // Reserved
          MAXDWORD,  // Lock all bytes (low DWORD)
          MAXDWORD,  // Lock all bytes (high DWORD)
          &overlapped
      );

      if (lock_result == 0) {
        // Lock failed: close file and return LockFileEx() error
        const DWORD lock_error = GetLastError();
        CloseHandle(handle);
        return {map_error(lock_error), INVALID_FILE_HANDLE};
      }
    }

    return {FilesystemResult::OK, handle};
  }

  OpenFileResult OpenForRead(
      const PlatformPath& path, bool hold_advisory_lock
  ) override {
    // Open file for reading, and allow concurrent reads and writes by other processes:
    // we rely on cooperative advisory locks, not Windows share modes, for file locking
    HANDLE handle = CreateFileW(
        path.Get(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,  // File must already exist
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (handle == INVALID_HANDLE_VALUE) {
      return {map_error(GetLastError()), INVALID_FILE_HANDLE};
    }

    // If advisory lock requested, attempt non-blocking exclusive lock
    if (hold_advisory_lock) {
      OVERLAPPED overlapped = {};
      const BOOL lock_result = LockFileEx(
          handle,
          LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
          0,
          MAXDWORD,
          MAXDWORD,
          &overlapped
      );

      if (lock_result == 0) {
        // Lock failed: close file and return LockFileEx() error
        const DWORD lock_error = GetLastError();
        CloseHandle(handle);
        return {map_error(lock_error), INVALID_FILE_HANDLE};
      }
    }

    return {FilesystemResult::OK, handle};
  }

  WriteResult Write(PlatformFileHandle handle, const char* src, size_t n) override {
    // Write exactly N bytes, looping on partial writes.
    // WriteFile() may write fewer bytes than requested, so we loop until all
    // data is written.
    DWORD total = 0;
    while (total < n) {
      DWORD written;
      const BOOL result = WriteFile(
          handle, src + total, static_cast<DWORD>(n - total), &written, nullptr
      );

      if (result == 0) {
        // Write failed - return partial bytes written
        return {map_error(GetLastError()), static_cast<size_t>(total)};
      }

      total += written;
    }

    return {FilesystemResult::OK, static_cast<size_t>(total)};
  }

  ReadResult Read(PlatformFileHandle handle, char* dst, size_t n) override {
    // Read up to N bytes
    DWORD bytes_read;
    const BOOL result = ReadFile(
        handle,
        dst,
        static_cast<DWORD>(n),
        &bytes_read,
        nullptr  // Synchronous I/O
    );

    if (result == 0) {
      return {map_error(GetLastError()), 0};
    }

    // Return number of bytes actually read, which may be 0 for EOF or < N if fewer
    // bytes than requested were available
    return {FilesystemResult::OK, static_cast<size_t>(bytes_read)};
  }

  FilesystemResult Close(PlatformFileHandle handle) override {
    // Close file handle: any advisory lock will be automatically released by Windows
    // when the handle is closed
    const BOOL result = CloseHandle(handle);
    if (result == 0) {
      return map_error(GetLastError());
    }
    return FilesystemResult::OK;
  }

  FilesystemResult Delete(const PlatformPath& path) override {
    const BOOL result = DeleteFileW(path.Get());
    if (result == 0) {
      return map_error(GetLastError());
    }
    return FilesystemResult::OK;
  }

  FilesystemResult DeleteDirectory(const PlatformPath& path) override {
    const BOOL result = RemoveDirectoryW(path.Get());
    if (result == 0) {
      return map_error(GetLastError());
    }
    return FilesystemResult::OK;
  }

  FilesystemResult Rename(const PlatformPath& src, const PlatformPath& dst) override {
    // Atomically rename src to dst: by default, MoveFileW() fails if dst exists,
    // which matches our no-clobber requirement
    const BOOL result = MoveFileW(src.Get(), dst.Get());
    if (result == 0) {
      return map_error(GetLastError());
    }
    return FilesystemResult::OK;
  }
};

std::unique_ptr<IFilesystem> CreateFilesystem() {
  return std::make_unique<WindowsFilesystem>();
}

}  // namespace datadog::impl
