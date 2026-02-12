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

class SdkStorage {
 public:
  explicit SdkStorage(IFilesystem& fs, uint32_t pid);

  bool Initialize(
      const impl::DiagnosticLogger& logger,
      std::string_view application_storage_path,
      std::string_view sdk_instance_name
  );

 private:
  IFilesystem& _fs;

  uint32_t _pid;
  std::array<char, 11> _pid_str_buffer{};
  std::string_view _pid_str;

  StoragePath _root;          // <application-storage-path>/.datadog/<sdk-instance-name>
  StoragePath _process_root;  // <_root>/<pid>
  StoragePath _events_root;   // <_root>/<pid>/events
};

}  // namespace datadog::impl
