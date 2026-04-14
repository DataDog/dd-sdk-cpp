// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/storage/artifact.hpp"

#include "datadog/impl/assert.hpp"
#include "datadog/impl/storage/filesystem_wrapper.hpp"
#include "datadog/impl/storage/util.hpp"

namespace datadog::impl {

ArtifactStorage::ArtifactStorage(IFilesystem& in_fs, DiagnosticLogger& in_logger)
    : _fs(in_fs), _logger(in_logger) {}

bool ArtifactStorage::Initialize(
    std::string_view datadog_root, std::string_view directory_name
) {
  const char* join_message =
      "Failed to initialize artifact storage directory from configured application "
      "storage path: path exceeds length limit";
  const char* mkdir_message =
      "Failed to initialize artifact storage directory from configured application "
      "storage path: unable to create directory";

  // Require a valid, dot-prefixed directory name: these names are provided by feature
  // implementations, so validation failure indicates an SDK bug
  if (directory_name.empty() || directory_name[0] != '.') {
    DATADOG_ASSERT(false, "Invalid artifact directory name");
    _logger.Error(
        "Unexpected artifact directory name: artifact directories must be non-empty "
        "and dot-prefixed",
        {{"directory_name", directory_name}}
    );
    return false;
  }

  // Build the path to <application-storage>/.datadog/<artifact-dir>, ensuring it fits
  // within our expected max path length
  if (!JoinPaths(_root, datadog_root, directory_name, _logger, join_message)) {
    return false;
  }

  // Create the directory if it doesn't exist, failing on creation error and succeeding
  // if the directory already exists
  FilesystemWrapper fsw(_fs);
  if (!EnsureDirectoryExists(_root, fsw, _logger, mkdir_message)) {
    return false;
  }

  // The desired artifact directory is valid: we can now permit reads and writes in that
  // directory
  return true;
}

}  // namespace datadog::impl
