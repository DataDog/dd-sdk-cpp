// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <array>
#include <optional>

#include "datadog/impl/diagnostics.hpp"
#include "datadog/impl/features/rum/command.hpp"
#include "datadog/impl/features/rum/scopes/view.hpp"

namespace datadog::impl {

/**
 * Container for the set of child RumViewScopes owned by a RumSessionScope.
 *
 * Only one view may be active at any given time, but scopes for inactive views may
 * remain open until all their pending resources are completed. The configured
 * std::array capacity should be sufficient to maintain all required view scopes.
 *
 * Capacity may be reached if the application continually creates new views while
 * existing views remain alive due to pending resources that are not completed in a
 * timely manner. Backpressure is handled by purging the oldest existing view scope so
 * that a new view scope may be created in its place.
 */
struct RumViewArray {
  DiagnosticLogger diagnostic_logger;
  std::array<std::optional<RumViewScope>, 8> items;

  explicit RumViewArray(const DiagnosticLogger& in_diagnostic_logger)
      : diagnostic_logger(in_diagnostic_logger) {}

  /**
   * Creates a new RumViewScope and adds it to the first available slot within the
   * array.
   *
   * If `Push()` is called while the array is at capacity, the oldest existing view
   * scope will be purged and replaced with the new scope, and a diagnostic warning will
   * be logged with `"backpressure_on":"rum-view-scope-array"`.
   */
  RumViewScope& Push(
      const RumScopeDependencies& deps,
      class RumSessionScope& parent,
      bool is_initial_view,
      const UUID& view_id,
      std::string_view key,
      std::string_view name,
      Timestamp start_time
  );

  /**
   * Propagates the given command to all scopes in the array, calling the `Process()`
   * function on each scope. Any scopes that return `RumScopeResult::Close` will be
   * removed from the array.
   *
   * Iteration order is not defined: no assumptions should be made about the order in
   * which sibling view scopes will process the command.
   */
  void Propagate(
      const RumCommand& command, const CoreContext& context, const EventWriter& writer
  );

  /**
   * Returns a reference to the first active view scope found in the array, or
   * std::nullopt if no such view scope exists.
   *
   * Only one view scope is expected to be active at any given time. If more than one
   * active view exists, no guarantees are made re: which one will be returned.
   */
  ScopeRef<const RumViewScope> FindActive() const;

 private:
  /**
   * Returns the index of the RumViewScope with the oldest started-at timestamp.
   */
  size_t FindOldest() const;
};

}  // namespace datadog::impl
