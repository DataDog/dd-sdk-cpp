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

ReadCrashContextResult ReadCrashContext(File& file) {
  // Parse header magic
  uint64_t magic{};
  if (auto res = ReadUInt64(file, magic); !res.OK()) {
    return {std::nullopt, res.value};
  }
  if (magic != CrashContextHeaderMagic) {
    return {std::nullopt, FilesystemResult::OK};
  }

  // Parse version magic: the only supported version is 1
  uint64_t version{};
  if (auto res = ReadUInt64(file, version); !res.OK()) {
    return {std::nullopt, res.value};
  }
  if (version != CrashContextFileVersion) {
    return {std::nullopt, FilesystemResult::OK};
  }

  // Default-construct a result value
  CrashContextFile ccf{};

  // Populate result struct's UUID fields with values read from the file
  if (auto res = ReadUUID(file, ccf.rum_application_id); !res.OK()) {
    return {std::nullopt, res.value};
  }
  if (auto res = ReadUUID(file, ccf.rum_session_id); !res.OK()) {
    return {std::nullopt, res.value};
  }
  if (auto res = ReadUUID(file, ccf.rum_view_id); !res.OK()) {
    return {std::nullopt, res.value};
  }
  if (auto res = ReadUUID(file, ccf.rum_action_id); !res.OK()) {
    return {std::nullopt, res.value};
  }

  // Parse footer magic: if not present, the file is not complete and should be ignored
  uint64_t footer{};
  if (auto res = ReadUInt64(file, footer); !res.OK()) {
    return {std::nullopt, res.value};
  }
  if (footer != CrashContextFooterMagic) {
    return {std::nullopt, FilesystemResult::OK};
  }

  return {ccf, FilesystemResult::OK};
}

}  // namespace datadog::impl
