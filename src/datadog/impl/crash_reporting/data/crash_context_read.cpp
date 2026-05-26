// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/crash_reporting/data/crash_context_read.hpp"

#include "datadog/impl/core/attribute/binary.hpp"
#include "datadog/impl/core/storage/filesystem_wrapper.hpp"
#include "datadog/impl/crash_reporting/data/crash_context.hpp"
#include "datadog/impl/crash_reporting/data/crash_read_util.hpp"

namespace datadog::impl {

// Max byte lengths for each string field, to guard against malformed or malicious data
static constexpr size_t MAX_SHORT_STRING_LEN = 4096;
static constexpr size_t MAX_VIEW_EVENT_LEN = 65536;

// All branches constitute a linear series of early-outs in case of file read failure
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
ReadCrashContextResult ReadCrashContext(File& file) {
  uint64_t magic{};
  if (auto res = ReadUInt64(file, magic); !res.OK()) {
    return {std::nullopt, res.value};
  }
  if (magic != CrashContextHeaderMagic) {
    return {std::nullopt, FilesystemResult::OK};
  }

  uint64_t version{};
  if (auto res = ReadUInt64(file, version); !res.OK()) {
    return {std::nullopt, res.value};
  }
  if (version != CrashContextFileVersion) {
    return {std::nullopt, FilesystemResult::OK};
  }

  CrashContext ctx{};

  // Application configuration details
  if (auto r = ReadString(file, ctx.service, MAX_SHORT_STRING_LEN); !r.OK()) {
    return {std::nullopt, r.value};
  }
  if (auto r = ReadString(file, ctx.env, MAX_SHORT_STRING_LEN); !r.OK()) {
    return {std::nullopt, r.value};
  }
  if (auto r = ReadString(file, ctx.application_version, MAX_SHORT_STRING_LEN);
      !r.OK()) {
    return {std::nullopt, r.value};
  }

  // Internal SDK configuration details
  if (auto r = ReadString(file, ctx.source, MAX_SHORT_STRING_LEN); !r.OK()) {
    return {std::nullopt, r.value};
  }
  if (auto r = ReadString(file, ctx.sdk_version, MAX_SHORT_STRING_LEN); !r.OK()) {
    return {std::nullopt, r.value};
  }

  // SDK instance state
  uint8_t tracking_consent_raw{};
  if (auto r = ReadUInt8(file, tracking_consent_raw); !r.OK()) {
    return {std::nullopt, r.value};
  }
  if (tracking_consent_raw > static_cast<uint8_t>(TrackingConsent::Pending)) {
    return {std::nullopt, FilesystemResult::OK};
  }
  ctx.tracking_consent = static_cast<TrackingConsent>(tracking_consent_raw);

  // OS info
  if (auto r = ReadString(file, ctx.os_name, MAX_SHORT_STRING_LEN); !r.OK()) {
    return {std::nullopt, r.value};
  }
  if (auto r = ReadString(file, ctx.os_version, MAX_SHORT_STRING_LEN); !r.OK()) {
    return {std::nullopt, r.value};
  }
  if (auto r = ReadString(file, ctx.os_build, MAX_SHORT_STRING_LEN); !r.OK()) {
    return {std::nullopt, r.value};
  }
  if (auto r = ReadString(file, ctx.os_version_major, MAX_SHORT_STRING_LEN); !r.OK()) {
    return {std::nullopt, r.value};
  }

  // Device info
  if (auto r = ReadString(file, ctx.device_type, MAX_SHORT_STRING_LEN); !r.OK()) {
    return {std::nullopt, r.value};
  }
  if (auto r = ReadString(file, ctx.device_name, MAX_SHORT_STRING_LEN); !r.OK()) {
    return {std::nullopt, r.value};
  }
  if (auto r = ReadString(file, ctx.device_model, MAX_SHORT_STRING_LEN); !r.OK()) {
    return {std::nullopt, r.value};
  }
  if (auto r = ReadString(file, ctx.device_brand, MAX_SHORT_STRING_LEN); !r.OK()) {
    return {std::nullopt, r.value};
  }
  if (auto r = ReadString(file, ctx.device_architecture, MAX_SHORT_STRING_LEN);
      !r.OK()) {
    return {std::nullopt, r.value};
  }
  if (auto r = ReadString(file, ctx.device_locale, MAX_SHORT_STRING_LEN); !r.OK()) {
    return {std::nullopt, r.value};
  }
  if (auto r = ReadString(file, ctx.device_time_zone, MAX_SHORT_STRING_LEN); !r.OK()) {
    return {std::nullopt, r.value};
  }

  // User info
  if (auto r = ReadString(file, ctx.user_id, MAX_SHORT_STRING_LEN); !r.OK()) {
    return {std::nullopt, r.value};
  }
  if (auto r = ReadString(file, ctx.user_name, MAX_SHORT_STRING_LEN); !r.OK()) {
    return {std::nullopt, r.value};
  }
  if (auto r = ReadString(file, ctx.user_email, MAX_SHORT_STRING_LEN); !r.OK()) {
    return {std::nullopt, r.value};
  }
  if (auto r = AttributeBinarySerialization::Parse(file, ctx.user_extra); !r.ok) {
    return {std::nullopt, r.fs_result};
  }

  // Account info
  if (auto r = ReadString(file, ctx.account_id, MAX_SHORT_STRING_LEN); !r.OK()) {
    return {std::nullopt, r.value};
  }
  if (auto r = ReadString(file, ctx.account_name, MAX_SHORT_STRING_LEN); !r.OK()) {
    return {std::nullopt, r.value};
  }
  if (auto r = AttributeBinarySerialization::Parse(file, ctx.account_extra); !r.ok) {
    return {std::nullopt, r.fs_result};
  }

  // RUM session state
  if (auto r = ReadUUID(file, ctx.rum_session_state.session_id); !r.OK()) {
    return {std::nullopt, r.value};
  }
  if (auto r = ReadBool(file, ctx.rum_session_state.is_sampled); !r.OK()) {
    return {std::nullopt, r.value};
  }
  if (auto r = ReadBool(file, ctx.rum_session_state.is_active); !r.OK()) {
    return {std::nullopt, r.value};
  }
  if (auto r = ReadBool(file, ctx.rum_session_state.is_initial_session); !r.OK()) {
    return {std::nullopt, r.value};
  }
  if (auto r = ReadBool(file, ctx.rum_session_state.has_tracked_any_view); !r.OK()) {
    return {std::nullopt, r.value};
  }
  if (auto r = ReadBool(file, ctx.rum_session_state.did_start_with_replay); !r.OK()) {
    return {std::nullopt, r.value};
  }

  // Last RUM view event (as JSON string)
  if (auto r = ReadString(file, ctx.last_view_event_json, MAX_VIEW_EVENT_LEN);
      !r.OK()) {
    return {std::nullopt, r.value};
  }

  // Global RUM Attributes
  if (auto r = AttributeBinarySerialization::Parse(file, ctx.global_rum_attributes);
      !r.ok) {
    return {std::nullopt, r.fs_result};
  }

  // Footer must be present and correct; its absence signals a truncated/corrupt file
  uint64_t footer{};
  if (auto r = ReadUInt64(file, footer); !r.OK()) {
    return {std::nullopt, r.value};
  }
  if (footer != CrashContextFooterMagic) {
    return {std::nullopt, FilesystemResult::OK};
  }

  return {ctx, FilesystemResult::OK};
}

}  // namespace datadog::impl
