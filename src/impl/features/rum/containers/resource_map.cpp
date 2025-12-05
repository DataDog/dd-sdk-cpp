// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "features/rum/containers/resource_map.hpp"

namespace datadog::impl {

RumResourceScope& RumResourceMap::Add(
    const RumScopeDependencies& deps,
    class RumViewScope& parent,
    const UUID& resource_id,
    const std::string& key,
    RumResourceMethod method,
    std::string_view url,
    Timestamp start_time,
    const Attribute& attributes
) {
  // With C++20 we could use `std::string_view key` for lookups, but C++17 does not
  // allow heterogenous lookup on std::unordered_map: therefore, we need to accept
  // `const std::string& key`, which may result in a copy if the argument value is a
  // string_view.
  auto existing = items.find(key);
  if (existing != items.end()) {
    diagnostic_logger.Warning(
        "Replacing existing RUM resource scope with a new one that shares the same "
        "key: application called StartResource twice with same key before StopResource",
        {{"key", key}}
    );
    items.erase(existing);
  }
  auto added = items.emplace(
      std::piecewise_construct,
      std::forward_as_tuple(key),
      std::forward_as_tuple(
          deps, parent, resource_id, key, method, url, start_time, attributes
      )
  );
  return added.first->second;
}

RumResourceScope::Result RumResourceMap::Forward(
    const std::string& resource_key, const RumCommand& command
) {
  // Prepare a result value, defaulting to 'no event'
  RumResourceScope::Result scope_result = RumResourceScope::Result::SentNoEvent;

  // See C++20 note above: we have to use `const std::string&` for the lookup
  auto found = items.find(resource_key);
  if (found == items.end()) {
    // Silently ignore the command if we have no matching resource scope: it's possible
    // that well-formed calls to StartResource and StopResource could occur on both
    // sides of the session expiration window
    return scope_result;
  }

  // We have a matching resource: propagate the command to that scope (and that scope
  // only), removing it from the map if it should be closed as a result
  const RumScopeResult result = found->second.Process(command);
  if (result == RumScopeResult::Close) {
    // If the scope is now closed, record its result (i.e. what kind of event it sent)
    // before destroying it
    scope_result = found->second.GetResult();
    items.erase(found);
  }

  // Return a result indicating what kind of event was produced, if any
  return scope_result;
}

}  // namespace datadog::impl
