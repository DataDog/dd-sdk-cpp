// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include "datadog/impl/diagnostics.hpp"
#include "datadog/impl/storage/filesystem.hpp"
#include "datadog/impl/storage/path.hpp"

namespace datadog::impl {

class EventStorage {
 public:
  explicit EventStorage(IFilesystem& fs, std::string_view feature_name);

  bool Initialize(const impl::DiagnosticLogger& logger, std::string_view events_root);

 private:
  IFilesystem& _fs;

  std::string_view _feature_name;

  StoragePath _pending_path;
  StoragePath _granted_path;
};

}  // namespace datadog::impl
