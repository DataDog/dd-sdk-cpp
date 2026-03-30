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

class FeatureEventStorage {
 public:
  explicit FeatureEventStorage(IFilesystem& in_fs, impl::DiagnosticLogger& in_logger);

  bool Initialize(std::string_view process_root, std::string_view feature_name);

  /**
   * Returns the path to the directory where batches of event data should be stored for
   * this feature, within this SDK instance, while tracking consent is pending.
   */
  std::string_view GetPendingPath() const;

  /**
   * Returns the path to the directory where batches of event data are stored for this
   * feature once the SDK instance has been granted tracking consent. This is the only
   * directory that events may be uploaded from.
   */
  std::string_view GetGrantedPath() const;

 private:
  IFilesystem& _fs;
  DiagnosticLogger& _logger;
  StoragePath _root;          // <app-storage>/.datadog/<instance>/<pid>/<feature>/
  StoragePath _pending_root;  // <_root>/intermediate-v1/
  StoragePath _granted_root;  // <_root>/v1/
};

}  // namespace datadog::impl
