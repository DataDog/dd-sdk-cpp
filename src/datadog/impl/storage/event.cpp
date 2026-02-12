// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/storage/event.hpp"

#include <charconv>

#include "datadog/impl/assert.hpp"
#include "datadog/impl/storage/util.hpp"

namespace datadog::impl {

EventStorage::EventStorage(IFilesystem& fs, std::string_view feature_name)
    : _fs(fs), _feature_name(feature_name) {}

bool EventStorage::Initialize(
    const impl::DiagnosticLogger& logger, std::string_view events_root
) {
  PlatformPath path;

  if (!_pending_path.Join(events_root, _feature_name)) {
    return false;
  }

  if (!EnsureDirectoryExists(
          _pending_path,
          path,
          _fs,
          logger,
          "Failed to initialize event storage: could not create feature subdirectory"
      )) {
    return false;
  }

  if (!_granted_path.Join(_pending_path.Get(), "v1")) {
    return false;
  }
  if (!_pending_path.Join(_pending_path.Get(), "intermediate-v1")) {
    return false;
  }

  if (!EnsureDirectoryExists(
          _pending_path,
          path,
          _fs,
          logger,
          "Failed to initialize event storage: could not create intermediate event "
          "storage directory"
      )) {
    return false;
  }

  if (!EnsureDirectoryExists(
          _granted_path,
          path,
          _fs,
          logger,
          "Failed to initialize event storage: could not create final event storage "
          "directory"
      )) {
    return false;
  }

  return true;
}

}  // namespace datadog::impl
