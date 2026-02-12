// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>

#include "datadog/impl/storage/filesystem.hpp"

namespace datadog::impl {

// === Error Mapping Helper ===
// Maps Windows GetLastError() codes to FilesystemResult codes.
namespace {
FilesystemResult MapWindowsError(DWORD error) {
  switch (error) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
      return FilesystemResult::ParentDirectoryDoesNotExist;
    case ERROR_ALREADY_EXISTS:
      return FilesystemResult::AlreadyExists;
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

// Converts a wide (UTF-16) string to a narrow (UTF-8) string for returning
// directory entry names as std::string. Returns empty string on conversion failure.
std::string WideToUtf8(const std::wstring& wide) {
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
}  // namespace

class WindowsFilesystem final : public IFilesystem {
 public:
  // === Directory Operations ===

  FilesystemResult CreateDirectory(const PlatformPath& path) override {
    // Attempt to create directory with default security attributes
    const BOOL result = CreateDirectoryW(path.Get(), nullptr);
    if (result != 0) {
      return FilesystemResult::OK;
    }

    // Creation failed, analyze GetLastError() to determine specific failure reason
    const DWORD error = GetLastError();
    if (error == ERROR_ALREADY_EXISTS) {
      // Path exists - use GetFileAttributesW() to distinguish file vs directory
      const DWORD attrs = GetFileAttributesW(path.Get());
      if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        return FilesystemResult::AlreadyExistsAsDirectory;
      }
    }
    return MapWindowsError(error);
  }

  FilesystemResult ListFiles(
      const PlatformPath& path, std::vector<std::string>& out_names
  ) override {
    out_names.clear();

    // Construct search pattern: path\*
    // PlatformPath returns const wchar_t*, so we build a wide string
    std::wstring search_pattern = path.Get();
    if (!search_pattern.empty() && search_pattern.back() != L'\\') {
      search_pattern += L'\\';
    }
    search_pattern += L'*';

    // Find first file in directory
    WIN32_FIND_DATAW find_data;
    HANDLE find_handle = FindFirstFileW(search_pattern.c_str(), &find_data);
    if (find_handle == INVALID_HANDLE_VALUE) {
      const DWORD error = GetLastError();
      if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
        return FilesystemResult::OK;  // Missing directory = empty list
      }
      return MapWindowsError(error);
    }

    // Iterate through directory entries, filtering for regular files
    do {
      // Skip directories - we only want regular files
      if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        // Convert wide filename to UTF-8 and add to output vector
        std::string filename = WideToUtf8(find_data.cFileName);
        if (!filename.empty()) {
          out_names.push_back(std::move(filename));
        }
      }
    } while (FindNextFileW(find_handle, &find_data) != 0);

    // Check if we exited loop due to error or end of directory
    const DWORD error = GetLastError();
    FindClose(find_handle);
    return error == ERROR_NO_MORE_FILES ? FilesystemResult::OK : MapWindowsError(error);
  }

  FilesystemResult ListSubdirectories(
      const PlatformPath& path, std::vector<std::string>& out_names
  ) override {
    out_names.clear();

    // Construct search pattern: path\*
    std::wstring search_pattern = path.Get();
    if (!search_pattern.empty() && search_pattern.back() != L'\\') {
      search_pattern += L'\\';
    }
    search_pattern += L'*';

    // Find first file in directory
    WIN32_FIND_DATAW find_data;
    HANDLE find_handle = FindFirstFileW(search_pattern.c_str(), &find_data);
    if (find_handle == INVALID_HANDLE_VALUE) {
      const DWORD error = GetLastError();
      if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
        return FilesystemResult::OK;  // Missing directory = empty list
      }
      return MapWindowsError(error);
    }

    // Iterate through directory entries, filtering for subdirectories
    do {
      // Only include directories, excluding "." and ".."
      if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        if (wcscmp(find_data.cFileName, L".") != 0 &&
            wcscmp(find_data.cFileName, L"..") != 0) {
          std::string dirname = WideToUtf8(find_data.cFileName);
          if (!dirname.empty()) {
            out_names.push_back(std::move(dirname));
          }
        }
      }
    } while (FindNextFileW(find_handle, &find_data) != 0);

    // Check if we exited loop due to error or end of directory
    const DWORD error = GetLastError();
    FindClose(find_handle);
    return error == ERROR_NO_MORE_FILES ? FilesystemResult::OK : MapWindowsError(error);
  }

  // === File Operations ===

  OpenFileResult OpenForWrite(
      const PlatformPath& path, bool append, bool hold_advisory_lock
  ) override {
    // Determine creation disposition: truncate or open/create for append
    // - TRUNCATE_EXISTING would fail if file doesn't exist, so use CREATE_ALWAYS
    //   to truncate or create
    // - OPEN_ALWAYS for append mode (create if doesn't exist, open if exists)
    const DWORD creation_disposition = append ? OPEN_ALWAYS : CREATE_ALWAYS;

    // Open file for writing. On Windows, FILE_APPEND_DATA restricts writes to
    // end of file (append mode), whereas GENERIC_WRITE allows arbitrary positioning.
    const DWORD desired_access = append ? FILE_APPEND_DATA : GENERIC_WRITE;

    // Open file with share mode allowing other processes to read (but not write)
    // unless we're taking a lock
    HANDLE handle = CreateFileW(
        path.Get(),
        desired_access,
        FILE_SHARE_READ,  // Allow concurrent reads
        nullptr,          // Default security
        creation_disposition,
        FILE_ATTRIBUTE_NORMAL,
        nullptr  // No template file
    );

    if (handle == INVALID_HANDLE_VALUE) {
      return {MapWindowsError(GetLastError()), INVALID_FILE_HANDLE};
    }

    // If advisory lock requested, attempt non-blocking exclusive lock
    if (hold_advisory_lock) {
      // LockFileEx() locks entire file range. Use LOCKFILE_EXCLUSIVE_LOCK for
      // exclusive access and LOCKFILE_FAIL_IMMEDIATELY for non-blocking.
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
        // Lock failed - close handle and return error
        const DWORD lock_error = GetLastError();
        CloseHandle(handle);
        return {MapWindowsError(lock_error), INVALID_FILE_HANDLE};
      }
    }

    return {FilesystemResult::OK, handle};
  }

  OpenFileResult OpenForRead(
      const PlatformPath& path, bool acquire_advisory_lock
  ) override {
    // Open file for reading. File must already exist (OPEN_EXISTING).
    // Allow concurrent reads and writes by other processes (unless locking).
    HANDLE handle = CreateFileW(
        path.Get(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (handle == INVALID_HANDLE_VALUE) {
      return {MapWindowsError(GetLastError()), INVALID_FILE_HANDLE};
    }

    // If advisory lock requested, attempt non-blocking shared lock
    if (acquire_advisory_lock) {
      // LockFileEx() without LOCKFILE_EXCLUSIVE_LOCK = shared lock
      OVERLAPPED overlapped = {};
      const BOOL lock_result = LockFileEx(
          handle,
          LOCKFILE_FAIL_IMMEDIATELY,  // Non-blocking shared lock
          0,
          MAXDWORD,
          MAXDWORD,
          &overlapped
      );

      if (lock_result == 0) {
        const DWORD lock_error = GetLastError();
        CloseHandle(handle);
        return {MapWindowsError(lock_error), INVALID_FILE_HANDLE};
      }
    }

    return {FilesystemResult::OK, handle};
  }

  WriteResult Write(PlatformFileHandle file, const char* src, size_t n) override {
    // Write exactly N bytes, looping on partial writes.
    // WriteFile() may write fewer bytes than requested, so we loop until all
    // data is written.
    DWORD total = 0;
    while (total < n) {
      DWORD written;
      const BOOL result = WriteFile(
          file,
          src + total,
          static_cast<DWORD>(n - total),
          &written,
          nullptr  // Synchronous I/O
      );

      if (result == 0) {
        // Write failed - return partial bytes written
        return {MapWindowsError(GetLastError()), static_cast<size_t>(total)};
      }

      total += written;
    }

    return {FilesystemResult::OK, static_cast<size_t>(total)};
  }

  ReadResult Read(PlatformFileHandle file, char* dst, size_t n) override {
    // Read up to N bytes in a single syscall (no looping).
    // Returns actual bytes read, which may be less than N (or 0 for EOF).
    DWORD bytes_read;
    const BOOL result = ReadFile(
        file,
        dst,
        static_cast<DWORD>(n),
        &bytes_read,
        nullptr  // Synchronous I/O
    );

    if (result == 0) {
      return {MapWindowsError(GetLastError()), 0};
    }

    return {FilesystemResult::OK, static_cast<size_t>(bytes_read)};
  }

  FilesystemResult Close(PlatformFileHandle file) override {
    // Close file handle. Advisory locks are automatically released by Windows
    // when the handle is closed.
    const BOOL result = CloseHandle(file);
    if (result == 0) {
      return MapWindowsError(GetLastError());
    }
    return FilesystemResult::OK;
  }

  FilesystemResult Delete(const PlatformPath& path) override {
    // Delete regular file. Returns error if path is a directory or doesn't exist.
    const BOOL result = DeleteFileW(path.Get());
    if (result == 0) {
      return MapWindowsError(GetLastError());
    }
    return FilesystemResult::OK;
  }

  FilesystemResult Rename(const PlatformPath& src, const PlatformPath& dst) override {
    // Atomically rename src to dst. By default, MoveFileW() fails if dst exists,
    // which matches our requirement (no clobber).
    const BOOL result = MoveFileW(src.Get(), dst.Get());
    if (result == 0) {
      return MapWindowsError(GetLastError());
    }
    return FilesystemResult::OK;
  }
};

}  // namespace datadog::impl
