// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/crash_reporting/data/crash_context_read.hpp"

#include "datadog/impl/core/storage/filesystem_wrapper.hpp"
#include "datadog/impl/crash_reporting/data/crash_context.hpp"
#include "datadog/impl/crash_reporting/data/crash_read_util.hpp"

namespace datadog::impl {

// Max byte lengths for each string field; guards against malformed or malicious data.
static constexpr size_t kMaxShortString = 4096;
static constexpr size_t kMaxJsonBlob = 65536;

static CrashFileReadResult read_bool(File& file, bool& out) {
  uint64_t value{};
  if (auto res = ReadUInt64(file, value); !res.OK()) {
    return res;
  }
  out = (value != 0);
  return {FilesystemResult::OK, true};
}

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

  // Core identity
  if (auto r = ReadString(file, ctx.service, kMaxShortString); !r.OK()) {
    return {std::nullopt, r.value};
  }
  if (auto r = ReadString(file, ctx.env, kMaxShortString); !r.OK()) {
    return {std::nullopt, r.value};
  }
  if (auto r = ReadString(file, ctx.application_version, kMaxShortString); !r.OK()) {
    return {std::nullopt, r.value};
  }
  if (auto r = ReadString(file, ctx.source, kMaxShortString); !r.OK()) {
    return {std::nullopt, r.value};
  }
  if (auto r = ReadString(file, ctx.sdk_version, kMaxShortString); !r.OK()) {
    return {std::nullopt, r.value};
  }

  uint64_t tracking_consent_raw{};
  if (auto r = ReadUInt64(file, tracking_consent_raw); !r.OK()) {
    return {std::nullopt, r.value};
  }
  ctx.tracking_consent = static_cast<TrackingConsent>(tracking_consent_raw);

  // OS info
  if (auto r = ReadString(file, ctx.os_name, kMaxShortString); !r.OK()) {
    return {std::nullopt, r.value};
  }
  if (auto r = ReadString(file, ctx.os_version, kMaxShortString); !r.OK()) {
    return {std::nullopt, r.value};
  }
  if (auto r = ReadString(file, ctx.os_build, kMaxShortString); !r.OK()) {
    return {std::nullopt, r.value};
  }
  if (auto r = ReadString(file, ctx.os_version_major, kMaxShortString); !r.OK()) {
    return {std::nullopt, r.value};
  }

  // Device info
  if (auto r = ReadString(file, ctx.device_type, kMaxShortString); !r.OK()) {
    return {std::nullopt, r.value};
  }
  if (auto r = ReadString(file, ctx.device_name, kMaxShortString); !r.OK()) {
    return {std::nullopt, r.value};
  }
  if (auto r = ReadString(file, ctx.device_model, kMaxShortString); !r.OK()) {
    return {std::nullopt, r.value};
  }
  if (auto r = ReadString(file, ctx.device_brand, kMaxShortString); !r.OK()) {
    return {std::nullopt, r.value};
  }
  if (auto r = ReadString(file, ctx.device_architecture, kMaxShortString); !r.OK()) {
    return {std::nullopt, r.value};
  }
  if (auto r = ReadString(file, ctx.device_locale, kMaxShortString); !r.OK()) {
    return {std::nullopt, r.value};
  }
  if (auto r = ReadString(file, ctx.device_time_zone, kMaxShortString); !r.OK()) {
    return {std::nullopt, r.value};
  }

  // User info
  if (auto r = ReadString(file, ctx.user_id, kMaxShortString); !r.OK()) {
    return {std::nullopt, r.value};
  }
  if (auto r = ReadString(file, ctx.user_name, kMaxShortString); !r.OK()) {
    return {std::nullopt, r.value};
  }
  if (auto r = ReadString(file, ctx.user_email, kMaxShortString); !r.OK()) {
    return {std::nullopt, r.value};
  }
  if (auto r = ReadString(file, ctx.user_extra_json, kMaxJsonBlob); !r.OK()) {
    return {std::nullopt, r.value};
  }

  // RUM session state
  if (auto r = ReadUUID(file, ctx.rum_session_state.session_id); !r.OK()) {
    return {std::nullopt, r.value};
  }
  if (auto r = read_bool(file, ctx.rum_session_state.is_sampled); !r.OK()) {
    return {std::nullopt, r.value};
  }
  if (auto r = read_bool(file, ctx.rum_session_state.is_active); !r.OK()) {
    return {std::nullopt, r.value};
  }
  if (auto r = read_bool(file, ctx.rum_session_state.is_initial_session); !r.OK()) {
    return {std::nullopt, r.value};
  }
  if (auto r = read_bool(file, ctx.rum_session_state.has_tracked_any_view); !r.OK()) {
    return {std::nullopt, r.value};
  }

  // JSON blobs
  if (auto r = ReadString(file, ctx.last_view_event_json, kMaxJsonBlob); !r.OK()) {
    return {std::nullopt, r.value};
  }
  if (auto r = ReadString(file, ctx.global_attributes_json, kMaxJsonBlob); !r.OK()) {
    return {std::nullopt, r.value};
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
