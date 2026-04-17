// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/crash_reporting/data/crash_context_write.hpp"

#include "datadog/impl/core/feature_types/rum.hpp"
#include "datadog/impl/core/storage/filesystem.hpp"
#include "datadog/impl/crash_reporting/data/crash_context.hpp"

namespace datadog::impl {

static bool write_bytes(
    IFilesystem& fs, PlatformFileHandle handle, const void* data, size_t size
) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  auto res = fs.Write(handle, reinterpret_cast<const char*>(data), size);
  return res.value == FilesystemResult::OK && res.bytes_written == size;
}

static bool write_uint64(IFilesystem& fs, PlatformFileHandle handle, uint64_t value) {
  return write_bytes(fs, handle, &value, sizeof(value));
}

bool WriteCrashContext(
    IFilesystem& fs,
    const PlatformPath& path,
    const PlatformPath& tmp_path,
    const RumFeatureContext& rum_ctx
) {
  // Write to <crash>.ctx.tmp first, to ensure that readers on next launch never observe
  // a partially-written file. We can truncate any existing file and disregard locking.
  const bool append = false;
  const bool hold_advisory_lock = false;
  const auto open_res = fs.OpenForWrite(tmp_path, append, hold_advisory_lock);
  if (open_res.value != FilesystemResult::OK) {
    return false;
  }
  const PlatformFileHandle handle = open_res.handle;

  // Write all required data to the temporary file
  bool write_ok = write_uint64(fs, handle, CrashContextHeaderMagic);
  if (write_ok) {
    write_ok = write_uint64(fs, handle, CrashContextFileVersion);
  }
  if (write_ok) {
    write_ok = write_bytes(fs, handle, rum_ctx.application_id.bytes.data(), 16);
  }
  if (write_ok) {
    write_ok = write_bytes(fs, handle, rum_ctx.session_id.bytes.data(), 16);
  }
  if (write_ok) {
    write_ok = write_bytes(fs, handle, rum_ctx.view_id.bytes.data(), 16);
  }
  if (write_ok) {
    write_ok = write_bytes(fs, handle, rum_ctx.action_id.bytes.data(), 16);
  }
  if (write_ok) {
    write_ok = write_uint64(fs, handle, CrashContextFooterMagic);
  }

  // Nothing more to write: close the .tmp file, ignoring failure
  const auto close_res = fs.Close(handle);
  (void)close_res;

  // If we didn't write a complete file, delete the .tmp file, effectively dropping the
  // context update and leaving any existing context intact, even though it may be out
  // of date
  if (!write_ok) {
    const auto delete_res = fs.Delete(tmp_path);
    (void)delete_res;
    return false;
  }

  // Wrote to .tmp file successfully: perform an atomic rename to clobber any existing
  // .ctx file, making the latest context values encoded in our .tmp file current
  auto replace_res = fs.ReplaceFile(tmp_path, path);
  if (replace_res != FilesystemResult::OK) {
    // Delete .ctx.tmp file if we failed to rename it to .ctx, leaving original .ctx
    const auto delete_res = fs.Delete(tmp_path);
    (void)delete_res;
    return false;
  }
  return true;
}

}  // namespace datadog::impl
