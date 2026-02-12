// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/storage/sdk.hpp"

#include <charconv>

#include "datadog/impl/assert.hpp"
#include "datadog/impl/storage/util.hpp"

namespace datadog::impl {

SdkStorage::SdkStorage(IFilesystem& fs, uint32_t pid) : _fs(fs), _pid(pid) {}

bool SdkStorage::Initialize(
    const impl::DiagnosticLogger& logger,
    std::string_view application_storage_path,
    std::string_view sdk_instance_name
) {
  PlatformPath path;

  // Form path to root <application-storage-path>/.datadog/ directory
  if (!_root.Join(application_storage_path, ".datadog")) {
    logger.Error(
        "Failed to initialize SDK storage: application storage path is invalid or too "
        "long",
        {{"parent_path", application_storage_path},
         {"directory_name", ".datadog"},
         {"max_storage_path_size", MAX_STORAGE_PATH_SIZE}}
    );
    return false;
  }
  if (!_root.Join(_root.Get(), sdk_instance_name)) {
    logger.Error(
        "Failed to initialize SDK storage: application storage path is too long",
        {{"parent_path", _root.Get()},
         {"directory_name", sdk_instance_name},
         {"max_storage_path_size", MAX_STORAGE_PATH_SIZE}}
    );
    return false;
  }

  // Ensure that root .datadog/ directory exists
  if (!EnsureDirectoryExists(
          _root,
          path,
          _fs,
          logger,
          "Failed to intialized SDK storage: could not create root .datadog/ storage "
          "directory in configured application storage path"
      )) {
    return false;
  }

  // Convert our PID to string for easy comparison and path-building
  auto res = std::to_chars(
      _pid_str_buffer.data(), _pid_str_buffer.data() + _pid_str_buffer.size() - 1, _pid
  );
  DATADOG_ASSERT(res.ec == std::errc{}, "Failed to convert uint32_t PID to string");
  *res.ptr = '\0';
  _pid_str = std::string_view{_pid_str_buffer.data()};

  if (!_process_root.Join(_root.Get(), _pid_str)) {
    logger.Error(
        "Failed to initialize SDK storage: application storage path is too long",
        {{"parent_path", _root.Get()},
         {"directory_name", _pid_str},
         {"max_storage_path_size", MAX_STORAGE_PATH_SIZE}}
    );
    return false;
  }

  if (!EnsureDirectoryExists(
          _process_root,
          path,
          _fs,
          logger,
          "Failed to initialize SDK storage: could not create process subdirectory "
          "within root .datadog/ storage directory"
      )) {
    return false;
  }

  if (!_events_root.Join(_process_root.Get(), "events")) {
    logger.Error(
        "Failed to initialize SDK storage: application storage path is too long",
        {{"parent_path", _process_root.Get()},
         {"directory_name", "events"},
         {"max_storage_path_size", MAX_STORAGE_PATH_SIZE}}
    );
    return false;
  }

  if (!EnsureDirectoryExists(
          _events_root,
          path,
          _fs,
          logger,
          "Failed to initialize SDK storage: could not create events subdirectory "
          "within process storage directory"
      )) {
    return false;
  }

  return true;
}

}  // namespace datadog::impl
