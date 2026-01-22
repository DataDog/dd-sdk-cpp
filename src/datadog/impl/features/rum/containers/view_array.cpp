// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/features/rum/containers/view_array.hpp"

#include <algorithm>
#include <limits>

#include "datadog/impl/assert.hpp"

namespace datadog::impl {

// Within this implementation file, we freely index into our std::array
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index)

RumViewScope& RumViewArray::Push(
    const RumScopeDependencies& deps,
    class RumSessionScope& parent,
    bool is_initial_view,
    const UUID& view_id,
    std::string_view key,
    std::string_view name,
    Timestamp start_time
) {
  // Find the first available array slot that holds std::nullopt
  size_t insert_index = std::numeric_limits<size_t>::max();
  for (size_t i = 0; i < items.max_size(); i++) {
    if (!items[i]) {
      insert_index = i;
      break;
    }
  }

  // If there's no available slot, attempt to purge the oldest view scope
  if (insert_index >= items.max_size()) {
    // Find the index of the oldest view, which should always succeed here
    insert_index = FindOldest();
    if (insert_index >= items.max_size()) {
      // In development builds, assert on unexpected failures
      DATADOG_ASSERT(false, "Failed to find oldest item in RumViewArray");

      // In production builds, log an error and default to using index 0
      diagnostic_logger.Error(
          "RUM view scope array is full and no oldest item was found; session has "
          "indeterminate child view scope state"
      );
      insert_index = 0;
    }

    // Log a warning to signal that we're dropping old views due to backpressure
    if (insert_index < items.max_size() && items[insert_index].has_value()) {
      // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
      const RumViewScope& scope = items[insert_index].value();
      diagnostic_logger.Warning(
          "Prematurely purging oldest RUM view scope: too many inactive views are "
          "still awaiting resource completion while new views are being started",
          {{"backpressure_on", "rum-view-scope-array"},
           {"array_index", insert_index},
           {"array_capacity", items.max_size()},
           {"view_id", scope.GetViewID()},
           {"view_key", scope.GetKey()}}
      );
    }
  }

  // We should have decided on an array slot to hold our new view scope
  DATADOG_ASSERT(
      insert_index < items.max_size(),
      "Failed to resolve valid index for RumViewScope emplace"
  );

  // Create the new view scope, and return a reference: the value will remain fixed at
  // this location in the array for its lifetime, so references may be stored
  // persistently for as long as the view scope remains open
  return items[insert_index].emplace(
      deps, parent, is_initial_view, view_id, key, name, start_time
  );
}

void RumViewArray::Propagate(const RumCommand& command) {
  // Iterate over all view scopes in the order in which they're stored, without respect
  // to creation time etc.
  for (auto& item : items) {
    // Skip empty slots
    if (item) {
      // Process the command, and clear the slot (destroying the RumViewScope) if the
      // scope should be closed as a result
      RumScopeResult result = item->Process(command);
      if (result == RumScopeResult::Close) {
        item.reset();
      }
    }
  }
}

ScopeRef<const RumViewScope> RumViewArray::FindActive() const {
  for (const auto& item : items) {
    if (item && item->IsActive()) {
      return item;
    }
  }
  return std::nullopt;
}

size_t RumViewArray::FindOldest() const {
  size_t oldest_item_index = std::numeric_limits<size_t>::max();
  Timestamp oldest_view_started_at{};
  for (size_t i = 0; i < items.max_size(); i++) {
    if (items[i]) {
      // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
      const Timestamp view_started_at = items[i]->GetStartedAt();
      if (oldest_item_index == std::numeric_limits<size_t>::max() ||
          view_started_at < oldest_view_started_at) {
        oldest_item_index = i;
        oldest_view_started_at = view_started_at;
      }
    }
  }
  return oldest_item_index;
}

// NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)

}  // namespace datadog::impl
