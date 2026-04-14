// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/storage/feature_event.hpp"

#include "datadog/impl/assert.hpp"
#include "datadog/impl/storage/util.hpp"

// NOLINTBEGIN(cppcoreguidelines-macro-usage)
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)

// Global version number applied to all event data stored persistently; may be bumped in
// the event of breaking changes in order to abandon previously-written events on disk.
// This versioning scheme applies to the storage implementation as a whole: individual
// features should implement their own versioning schemes internally if needed.
#define DATADOG_EVENT_STORAGE_VERSION "1"

// Use (e.g.) 'v1' to store events gathered while tracking consent is granted;
// 'intermediate-v1' for events gathered while tracking consent is pending
const char* PENDING_SUBDIRECTORY_NAME = "intermediate-v" DATADOG_EVENT_STORAGE_VERSION;
const char* GRANTED_SUBDIRECTORY_NAME = "v" DATADOG_EVENT_STORAGE_VERSION;

// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)
// NOLINTEND(cppcoreguidelines-macro-usage)

namespace datadog::impl {

FeatureEventStorage::FeatureEventStorage(
    IFilesystem& in_fs, impl::DiagnosticLogger& in_logger
)
    : _fs(in_fs), _logger(in_logger) {}

bool FeatureEventStorage::Initialize(
    std::string_view process_root, std::string_view feature_name
) {
  const char* join_message =
      "Failed to initialize feature event storage directory from configured "
      "application storage path: path exceeds length limit";
  const char* mkdir_message =
      "Failed to initialize feature event storage directory from configured "
      "application storage path: unable to create directory";

  // Temporary buffer used for path encoding on platforms that require it
  PlatformPath path;

  // process_root gives us the root directory for all events stored by this SDK
  // instance, i.e. <application-storage>/.datadog/<instance>/<pid>/ - we first need to
  // build a root directory for our feature within process_root
  if (!JoinPaths(_root, process_root, feature_name, _logger, join_message)) {
    return false;
  }
  if (!EnsureDirectoryExists(_root, path, _fs, _logger, mkdir_message)) {
    return false;
  }

  // Now that we have a root feature directory, we want two subdirectories: the first,
  // _pending_root, is for events gathered while tracking consent is pending
  if (!JoinPaths(
          _pending_root, _root.Get(), PENDING_SUBDIRECTORY_NAME, _logger, join_message
      )) {
    return false;
  }
  if (!EnsureDirectoryExists(_pending_root, path, _fs, _logger, mkdir_message)) {
    return false;
  }

  // The second, _granted_root, contains events that we have consent to upload
  if (!JoinPaths(
          _granted_root, _root.Get(), GRANTED_SUBDIRECTORY_NAME, _logger, join_message
      )) {
    return false;
  }
  if (!EnsureDirectoryExists(_granted_root, path, _fs, _logger, mkdir_message)) {
    return false;
  }

  // We can now permit the SDK to write events to pending and/or granted dirs; read from
  // the granted dir; and freely move/delete files in those directories
  return true;
}

std::string_view FeatureEventStorage::GetPendingPath() const {
  return _pending_root.Get();
}

std::string_view FeatureEventStorage::GetGrantedPath() const {
  return _granted_root.Get();
}

}  // namespace datadog::impl
