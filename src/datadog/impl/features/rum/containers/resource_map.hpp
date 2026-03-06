// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <string>
#include <unordered_map>

#include "datadog/impl/diagnostics.hpp"
#include "datadog/impl/features/rum/command.hpp"
#include "datadog/impl/features/rum/scopes/resource.hpp"

namespace datadog::impl {

/**
 * Container for the set of child RumResourceScopes owned by a RumViewScope.
 *
 * A view can have any number of resources active at any given time: a view may never
 * create _any_ resources, while another view may conceivably make ~10-100 concurrent
 * concurrent HTTP requests that need to be tracked. Therefore, we store
 * RumResourceScopes on the heap, with no predetermined limit on the number of scopes
 * that can exist concurrently.
 */
struct RumResourceMap {
  DiagnosticLogger diagnostic_logger;
  std::unordered_map<std::string, RumResourceScope> items;

  explicit RumResourceMap(const DiagnosticLogger& in_diagnostic_logger)
      : diagnostic_logger(in_diagnostic_logger) {}

  /**
   * Creates a new RumResourceScope and adds it to the map, indexed by its key.
   *
   * If the map already contains a RumResourceScope with the given key, logs a warning
   * and replaces that scope with the new value.
   */
  RumResourceScope& Add(
      const RumScopeDependencies& deps,
      class RumViewScope& parent,
      const UUID& resource_id,
      const std::string& key,
      RumResourceMethod method,
      std::string_view url,
      Timestamp start_time,
      const Attribute& attributes
  );

  /**
   * Routes the given command to the scope with the given key, calling
   * RumResourceScope::Process() on the matching scope if found, and removing the scope
   * from the map (destroying it) if Process() returns RumScopeResult::Close.
   *
   * If the map does not contain a scope matching the given key, has no effect.
   *
   * Returns an enum value indicating whether processing of the command resulted in a
   * resource event, and error event, or no event.
   */
  RumResourceScope::Result Forward(
      const std::string& resource_key,
      const RumCommand& command,
      const CoreContext& context,
      const EventWriter& writer
  );

  /**
   * Returns the number of open resource scopes that are currently held in this map.
   */
  size_t Size() const { return items.size(); }
};

}  // namespace datadog::impl
