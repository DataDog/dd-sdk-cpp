// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cstddef>
#include <optional>

namespace datadog::impl {

/**
 * Describes how the storage thread should enforce size limits and other constraints on
 * the batch files for a specific feature.
 *
 * Every value is an override of a default that the Core applies to all features: a
 * value that is not set leaves that default in place. A feature only needs to set the
 * limits that its events need to differ on.
 */
struct FeatureStorageConfig {
  /**
   * The most events that may be written to a single batch file. Set to 1 for a feature
   * whose upload must be given exactly one event, as each batch file becomes one upload
   * request.
   */
  std::optional<int> max_writes_per_file;

  /**
   * The largest a single event may be, counting its metadata and the TLV headers stored
   * with it. A larger event is dropped.
   */
  std::optional<size_t> max_event_size;

  /**
   * The largest a batch file may be. Events that would not fit in a file are dropped
   * even if they are within `max_event_size`, so this should be at least as large.
   */
  std::optional<size_t> max_file_size;
};

}  // namespace datadog::impl
