// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/platform/crash_context_write.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "datadog/impl/core/feature_types/rum.hpp"
#include "datadog/impl/core/platform/crash_context.hpp"

namespace datadog::platform {

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg)

#ifdef _WIN32
using FileHandle = HANDLE;
static const FileHandle kInvalidHandle = INVALID_HANDLE_VALUE;
#else
using FileHandle = int;
static const FileHandle kInvalidHandle = -1;
#endif

static void write_bytes(FileHandle fd, const void* data, size_t size) {
#ifdef _WIN32
  DWORD written = 0;
  WriteFile(fd, data, static_cast<DWORD>(size), &written, nullptr);
  (void)written;
#else
  ssize_t result = write(fd, data, size);
  (void)result;
#endif
}

static void write_uint64(FileHandle fd, uint64_t value) {
  write_bytes(fd, &value, sizeof(value));
}

void WriteCrashContext(const char* filename, const impl::RumFeatureContext& rum_ctx) {
  // Write to a .tmp file first, then atomically rename to the final path. This
  // ensures readers on the next launch never observe a partially-written file.
  char tmp[512];
#ifdef _WIN32
  _snprintf_s(tmp, sizeof(tmp), _TRUNCATE, "%s.tmp", filename);
#else
  snprintf(tmp, sizeof(tmp), "%s.tmp", filename);
#endif

#ifdef _WIN32
  FileHandle fd = CreateFileA(
      tmp, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr
  );
#else
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
  FileHandle fd = open(tmp, O_CREAT | O_WRONLY | O_TRUNC, 0644);
#endif

  if (fd == kInvalidHandle) {
    return;
  }

  write_uint64(fd, CrashContextHeaderMagic);
  write_uint64(fd, CrashContextFileVersion);
  write_bytes(fd, rum_ctx.application_id.bytes.data(), 16);
  write_bytes(fd, rum_ctx.session_id.bytes.data(), 16);
  write_bytes(fd, rum_ctx.view_id.bytes.data(), 16);
  write_bytes(fd, rum_ctx.action_id.bytes.data(), 16);
  write_uint64(fd, CrashContextFooterMagic);

#ifdef _WIN32
  CloseHandle(fd);
  // MoveFileExA with MOVEFILE_REPLACE_EXISTING is the closest Windows equivalent
  // to an atomic rename; it replaces the destination in a single operation
  MoveFileExA(tmp, filename, MOVEFILE_REPLACE_EXISTING);
#else
  close(fd);
  // rename() is atomic on POSIX when src and dst are on the same filesystem,
  // which is guaranteed here since both paths share the same .crashes/ directory
  rename(tmp, filename);
#endif
}

void DeleteCrashContext(const char* filename) {
  char tmp[512];
#ifdef _WIN32
  DeleteFileA(filename);
  _snprintf_s(tmp, sizeof(tmp), _TRUNCATE, "%s.tmp", filename);
  DeleteFileA(tmp);
#else
  unlink(filename);
  snprintf(tmp, sizeof(tmp), "%s.tmp", filename);
  unlink(tmp);
#endif
}

// NOLINTEND(cppcoreguidelines-pro-type-vararg)
// NOLINTEND(cppcoreguidelines-pro-bounds-array-to-pointer-decay)

}  // namespace datadog::platform
