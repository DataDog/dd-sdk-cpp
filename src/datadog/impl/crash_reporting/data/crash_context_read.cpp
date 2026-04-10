// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/crash_reporting/data/crash_context_read.hpp"

#include "datadog/impl/core/storage/filesystem_wrapper.hpp"
#include "datadog/impl/crash_reporting/data/crash_context.hpp"

namespace datadog::impl {

static bool read_bytes(File& file, char* dst, size_t n) {
  auto res = file.Read(dst, n);
  return res.value == FilesystemResult::OK && res.bytes_read == n;
}

static bool read_uint64(File& file, uint64_t& out) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  return read_bytes(file, reinterpret_cast<char*>(&out), sizeof(out));
}

static bool read_uuid(File& file, UUID& out_uuid) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  return read_bytes(file, reinterpret_cast<char*>(out_uuid.bytes.data()), 16);
}

std::optional<CrashContextFile> ReadCrashContext(File& file) {
  // Parse header magic
  uint64_t magic{};
  if (!read_uint64(file, magic) || magic != CrashContextHeaderMagic) {
    return std::nullopt;
  }

  // Parse version magic: the only supported version is 1
  uint64_t version{};
  if (!read_uint64(file, version) || version != CrashContextFileVersion) {
    return std::nullopt;
  }

  // Default-construct a result value; use std::optional to ensure NRVO eligibility
  std::optional<CrashContextFile> result{std::in_place};

  // Populate result struct's UUID fields with values read from the file
  if (!read_uuid(file, result->rum_application_id)) {
    return std::nullopt;
  }
  if (!read_uuid(file, result->rum_session_id)) {
    return std::nullopt;
  }
  if (!read_uuid(file, result->rum_view_id)) {
    return std::nullopt;
  }
  if (!read_uuid(file, result->rum_action_id)) {
    return std::nullopt;
  }

  // Parse footer magic: if not present, the file is not complete and should be ignored
  uint64_t footer{};
  if (!read_uint64(file, footer) || footer != CrashContextFooterMagic) {
    return std::nullopt;
  }

  return result;
}

}  // namespace datadog::impl
