// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <string_view>

#include "datadog/impl/diagnostics.hpp"
#include "datadog/impl/storage/filesystem.hpp"
#include "datadog/impl/storage/path.hpp"

namespace datadog::impl {

class ArtifactStorage {
 public:
  explicit ArtifactStorage(IFilesystem& in_fs, DiagnosticLogger& in_logger);

  bool Initialize(std::string_view datadog_root, std::string_view directory_name);

  std::string_view GetPath() const;

 private:
  IFilesystem& _fs;
  DiagnosticLogger& _logger;
  StoragePath _root;  // <app-storage>/.datadog/<artifact-dir-name>
};

}  // namespace datadog::impl