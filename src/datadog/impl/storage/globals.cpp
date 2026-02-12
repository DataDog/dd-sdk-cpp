// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/storage/globals.hpp"

#include "datadog/impl/storage/path.hpp"

namespace datadog::impl {

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static StoragePath s_application_storage_path;

bool SetDefaultApplicationStoragePath(std::string_view path) {
  return s_application_storage_path.Set(path);
}

std::string_view GetDefaultApplicationStoragePath() {
  return s_application_storage_path.Get();
}

}  // namespace datadog::impl
