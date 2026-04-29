// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/crash_reporting/data/crash_context_write.hpp"

#include "datadog/impl/core/feature_types/crash_reporting.hpp"
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

static bool write_string(
    IFilesystem& fs, PlatformFileHandle handle, std::string_view s
) {
  const uint64_t len = s.size();
  if (!write_uint64(fs, handle, len)) {
    return false;
  }
  if (len == 0) {
    return true;
  }
  return write_bytes(fs, handle, s.data(), len);
}

bool WriteCrashContext(
    IFilesystem& fs,
    const PlatformPath& path,
    const PlatformPath& tmp_path,
    const CrashContext& ctx
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

  const auto& s = ctx.rum_session_state;
  // clang-format off
  const bool ok =
      write_uint64(fs, handle, CrashContextHeaderMagic) &&
      write_uint64(fs, handle, CrashContextFileVersion) &&
      // Core identity
      write_string(fs, handle, ctx.service) &&
      write_string(fs, handle, ctx.env) &&
      write_string(fs, handle, ctx.application_version) &&
      write_string(fs, handle, ctx.source) &&
      write_string(fs, handle, ctx.sdk_version) &&
      write_uint64(fs, handle, static_cast<uint64_t>(ctx.tracking_consent)) &&
      // OS info
      write_string(fs, handle, ctx.os_name) &&
      write_string(fs, handle, ctx.os_version) &&
      write_string(fs, handle, ctx.os_build) &&
      write_string(fs, handle, ctx.os_version_major) &&
      // Device info
      write_string(fs, handle, ctx.device_type) &&
      write_string(fs, handle, ctx.device_name) &&
      write_string(fs, handle, ctx.device_model) &&
      write_string(fs, handle, ctx.device_brand) &&
      write_string(fs, handle, ctx.device_architecture) &&
      write_string(fs, handle, ctx.device_locale) &&
      write_string(fs, handle, ctx.device_time_zone) &&
      // User info
      write_string(fs, handle, ctx.user_id) &&
      write_string(fs, handle, ctx.user_name) &&
      write_string(fs, handle, ctx.user_email) &&
      write_string(fs, handle, ctx.user_extra_json) &&
      // RUM session state
      write_bytes(fs, handle, s.session_id.bytes.data(), 16) &&
      write_uint64(fs, handle, s.is_sampled ? 1U : 0U) &&
      write_uint64(fs, handle, s.is_active ? 1U : 0U) &&
      write_uint64(fs, handle, s.is_initial_session ? 1U : 0U) &&
      write_uint64(fs, handle, s.has_tracked_any_view ? 1U : 0U) &&
      // JSON blobs
      write_string(fs, handle, ctx.last_view_event_json) &&
      write_string(fs, handle, ctx.global_attributes_json) &&
      write_uint64(fs, handle, CrashContextFooterMagic);
  // clang-format on

  // Nothing more to write: close the .tmp file, ignoring failure
  const auto close_res = fs.Close(handle);
  (void)close_res;

  // If we didn't write a complete file, delete the .tmp file, effectively dropping the
  // context update and leaving any existing context intact, even though it may be out
  // of date
  if (!ok) {
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
