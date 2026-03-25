// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <string_view>

#include "datadog/impl/storage/filesystem.hpp"

namespace datadog::impl {

const char* FilesystemResultStr(FilesystemResult result);

bool AppendPath(
    class StoragePath& dst,
    std::string_view name,
    const class DiagnosticLogger& logger,
    const char* failure_message
);

bool JoinPaths(
    class StoragePath& dst,
    std::string_view parent,
    std::string_view name,
    const class DiagnosticLogger& logger,
    const char* failure_message
);

bool EnsureDirectoryExists(
    const class StoragePath& path,
    class PlatformPath& platform_path,
    class IFilesystem& fs,
    const class DiagnosticLogger& logger,
    const char* failure_message
);

}  // namespace datadog::impl
