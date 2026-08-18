// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/crash_reporting/data/crash_context_write.hpp"

#include <cstring>

#include "datadog/impl/core/attribute/binary.hpp"
#include "datadog/impl/core/block.hpp"
#include "datadog/impl/core/feature_types/crash_reporting.hpp"
#include "datadog/impl/core/storage/filesystem.hpp"
#include "datadog/impl/core/storage/filesystem_wrapper.hpp"
#include "datadog/impl/crash_reporting/data/crash_context.hpp"

namespace datadog::impl {

bool WriteCrashContext(
    IFilesystem& fs,
    const PlatformPath& path,
    const PlatformPath& tmp_path,
    std::vector<char>& encode_buf,
    const CrashContext& ctx
) {
  // We don't want to eat the syscall overhead of writing each value individually: we'll
  // encode contiguous chunks of binary data into the buffer before writing to the file
  encode_buf.clear();

  // We'll pre-encode in several chunks: estimate the worst-case size of one of those
  // chunks and ensure that our buffer is large enough to fit it
  const size_t first_chunk_size =
      16 + (8 + ctx.service.size()) + (8 + ctx.env.size()) +
      (8 + ctx.application_version.size()) + (8 + ctx.source.size()) +
      (8 + ctx.sdk_version.size()) + (8 + ctx.variant.size()) + 1 +
      (8 + ctx.os_name.size()) + (8 + ctx.os_version.size()) +
      (8 + ctx.os_build.size()) + (8 + ctx.os_version_major.size()) +
      (8 + ctx.device_type.size()) + (8 + ctx.device_name.size()) +
      (8 + ctx.device_model.size()) + (8 + ctx.device_brand.size()) +
      (8 + ctx.device_architecture.size()) + (8 + ctx.device_locale.size()) +
      (8 + ctx.device_time_zone.size()) + (8 + ctx.user_id.size()) +
      (8 + ctx.user_name.size()) + (8 + ctx.user_email.size()) + 16;
  const size_t second_chunk_size =
      (8 + ctx.account_id.size()) + (8 + ctx.account_name.size());
  const size_t third_chunk_size =
      16 + 1 + 1 + 1 + 1 + 1 + (8 + ctx.last_view_event_json.size());
  size_t encode_buf_capacity = 2048;
  encode_buf_capacity = std::max(encode_buf_capacity, first_chunk_size);
  encode_buf_capacity = std::max(encode_buf_capacity, second_chunk_size);
  encode_buf_capacity = std::max(encode_buf_capacity, third_chunk_size);
  encode_buf.reserve(QuantizeBufferSize(encode_buf_capacity));

  // Establish some utility functions to accumulate the binary data we'll write
  auto encode_uint8 = [&](uint8_t value) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const char* bytes = reinterpret_cast<const char*>(&value);
    encode_buf.insert(encode_buf.end(), bytes, bytes + sizeof(value));
  };

  auto encode_uint64 = [&](uint64_t value) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const char* bytes = reinterpret_cast<const char*>(&value);
    encode_buf.insert(encode_buf.end(), bytes, bytes + sizeof(value));
  };

  auto encode_string = [&](std::string_view value) {
    encode_uint64(value.size());
    encode_buf.insert(encode_buf.end(), value.begin(), value.end());
  };

  auto encode_uuid = [&](const UUID& value) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const char* bytes = reinterpret_cast<const char*>(value.bytes.data());
    encode_buf.insert(encode_buf.end(), bytes, bytes + value.bytes.size());
  };

  // Write to <crash>.ctx.tmp first, to ensure that readers on next launch never observe
  // a partially-written file. We can truncate any existing file and disregard locking.
  const bool append = false;
  const bool hold_advisory_lock = false;
  const auto open_res = fs.OpenForWrite(tmp_path, append, hold_advisory_lock);
  if (open_res.value != FilesystemResult::OK) {
    return false;
  }
  File tmp_file(fs, open_res.handle);

  // Use a helper function to flush all data from encode_buf into the file, deleting it
  // on failure
  auto write_encoded_data = [&]() -> FilesystemResult {
    auto res = tmp_file.Write(encode_buf.data(), encode_buf.size());
    if (res.value != FilesystemResult::OK) {
      fs.Delete(tmp_path);
    }
    return res.value;
  };

  // BEGIN CONTEXT FILE CONTENTS with magic and binary format version
  encode_uint64(CrashContextHeaderMagic);
  encode_uint64(CrashContextFileVersion);

  // Application configuration details
  encode_string(ctx.service);
  encode_string(ctx.env);
  encode_string(ctx.application_version);
  encode_string(ctx.variant);

  // Internal SDK configuration details
  encode_string(ctx.source);
  encode_string(ctx.sdk_version);

  // Current SDK instance state
  encode_uint8(static_cast<uint8_t>(ctx.tracking_consent));

  // OS info
  encode_string(ctx.os_name);
  encode_string(ctx.os_version);
  encode_string(ctx.os_build);
  encode_string(ctx.os_version_major);

  // Device info
  encode_string(ctx.device_type);
  encode_string(ctx.device_name);
  encode_string(ctx.device_model);
  encode_string(ctx.device_brand);
  encode_string(ctx.device_architecture);
  encode_string(ctx.device_locale);
  encode_string(ctx.device_time_zone);

  // User info
  encode_string(ctx.user_id);
  encode_string(ctx.user_name);
  encode_string(ctx.user_email);
  encode_uuid(ctx.user_anonymous_id);
  // (end of first chunk: flush all data buffered so far, then write user attributes)
  if (write_encoded_data() != FilesystemResult::OK) {
    return false;
  }
  if (auto res = AttributeBinarySerialization::Write(ctx.user_extra, tmp_file);
      !res.ok) {
    return false;
  }

  // Account info
  encode_buf.clear();  // begin second chunk
  encode_string(ctx.account_id);
  encode_string(ctx.account_name);
  // (end of second chunk: flush all data, then write extra account attributes)
  if (write_encoded_data() != FilesystemResult::OK) {
    return false;
  }
  if (auto res = AttributeBinarySerialization::Write(ctx.account_extra, tmp_file);
      !res.ok) {
    return false;
  }

  // RumSessionState
  encode_buf.clear();  // begin third chunk
  encode_uuid(ctx.rum_session_state.application_id);
  encode_uuid(ctx.rum_session_state.session_id);
  encode_uint8(ctx.rum_session_state.is_sampled ? 1 : 0);
  encode_uint8(ctx.rum_session_state.is_active ? 1 : 0);
  encode_uint8(ctx.rum_session_state.is_initial_session ? 1 : 0);
  encode_uint8(ctx.rum_session_state.has_tracked_any_view ? 1 : 0);
  encode_uint8(ctx.rum_session_state.did_start_with_replay ? 1 : 0);

  // Last RUM view event
  encode_string(ctx.last_view_event_json);

  // (end of third chunk; flush all data buffered so far)
  if (write_encoded_data() != FilesystemResult::OK) {
    return false;
  }

  // Global RUM attributes
  if (auto res =
          AttributeBinarySerialization::Write(ctx.global_rum_attributes, tmp_file);
      !res.ok) {
    return false;
  }

  // END CONTEXT FILE CONTENTS with magic to indicate complete file
  encode_buf.clear();  // begin fourth chunk
  encode_uint64(CrashContextFooterMagic);
  if (write_encoded_data() != FilesystemResult::OK) {
    return false;
  }

  // We've successfully written all data to the .ctx.tmp file: perform an atomic rename
  // to clobber any existing .ctx file, making the latest context values encoded in our
  // .tmp file current
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
