// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <string_view>

#include "datadog/impl/core/storage/filesystem.hpp"
#include "datadog/impl/core/storage/path.hpp"
#include "datadog/impl/core/util/diagnostics.hpp"

namespace datadog::impl {

/**
 * Provides access to a process-wide artifact storage directory at
 * <application-storage>/.datadog/<directory-name>/.
 *
 * Unlike event storage, artifact storage directories are shared across all SDK
 * instances and PIDs. Features that use artifact storage are responsible for managing
 * multi-process contention themselves (e.g. via advisory locks).
 */
class ArtifactStorage {
 public:
  explicit ArtifactStorage(IFilesystem& in_fs, DiagnosticLogger& in_logger);

  /**
   * Given the path to <application-storage>/.datadog/, creates a subdirectory within
   * the given name, if none yet exists.
   *
   * The directory name must be prefixed with a dot, in order to differentiate artifact
   * directories from the instance-level directories used for event storage.
   *
   * Returns true if the desired directory now exists.
   */
  bool Initialize(std::string_view datadog_root, std::string_view directory_name);

  /**
   * Returns the full path to the artifact directory. May only be called after
   * Initialize() has completed successfully.
   */
  const StoragePath& GetPath() const { return _root; }

 private:
  IFilesystem& _fs;
  DiagnosticLogger& _logger;
  StoragePath _root;  // <app-storage>/.datadog/<artifact-dir-name>
};

}  // namespace datadog::impl
