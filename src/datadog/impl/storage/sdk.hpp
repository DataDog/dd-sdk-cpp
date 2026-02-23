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

  ~SdkStorage();

  bool Initialize(
      const impl::DiagnosticLogger& logger,
      std::string_view application_storage_path,
      std::string_view sdk_instance_name
  );

  void MigrateAbandonedEvents();

 private:
  void HandleMigrate(std::string_view from_pid);

 private:
  IFilesystem& _fs;

  uint32_t _pid;
  std::array<char, 11> _pid_str_buffer{};
  std::string_view _pid_str;

  StoragePath _root;          // <application-storage-path>/.datadog/
  StoragePath _process_root;  // <_root>/<pid>/
  StoragePath _events_root;   // <_root>/<pid>/<sdk-instance-name>/

  PlatformFileHandle _lockfile_handle{INVALID_FILE_HANDLE};
};

}  // namespace datadog::impl
