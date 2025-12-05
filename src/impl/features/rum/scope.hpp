// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <algorithm>
#include <cinttypes>
#include <functional>
#include <mutex>
#include <optional>
#include <random>
#include <shared_mutex>
#include <string>
#include <vector>

#include "attribute/typed_attribute.hpp"
#include "core/feature_scope.hpp"
#include "diagnostics.hpp"
#include "features/rum/command.hpp"
#include "platform/clock.hpp"

namespace datadog {
struct RumConfig;
}

namespace datadog::impl {

/**
 * Immutable set of input values used during RumScope processing.
 */
struct RumScopeDependencies {
  // Configuration details
  UUID application_id;
  DiagnosticLogger diagnostic_logger;
  const platform::IClock& clock;

  // Reference to the Feature's interface to the core, used for accessing context,
  // generating events, etc. FeatureScopes aren't created until Core::Start(), and they
  // only last until Core::Stop(), so this value is only guaranteed to be non-NULL while
  // the SDK is running.
  FeatureScope* scope;

 private:
  // Synchronization for internal state
  mutable std::shared_mutex mutex;

  // Internal state used in sampling decisions
  float _sampling_rate_unit;
  mutable std::mt19937 _sampling_rng;
  mutable std::uniform_real_distribution<float> _sampling_distribution;

  // Internal state for encoding RUM event payloads
  mutable std::vector<uint8_t> _encode_buffer;

 public:
  explicit RumScopeDependencies(
      const RumConfig& config, const platform::IClock& in_clock
  );

  /**
   * Injects required dependencies that are only initialized upon SDK start.
   */
  void OnStart(FeatureScope& in_scope);

  /**
   * Clears any dependencies that were injected on SDK start.
   */
  void OnStop();

 public:
  /**
   * Makes a single sampling decision for a newly-created session. If the result is
   * true, the session should be sampled, meaning that it should process the full set of
   * commands and generate RUM events to be sent to intake. If false, the session should
   * not be sampled, meaning it will ignore most commands and generate no events.
   */
  bool ShouldSampleSession() const;

  template <typename T>
  void ProduceEvent(const T& event) const {
    if (scope) {
      std::unique_lock write_lock(mutex);
      EncodeJson(_encode_buffer, event);
      std::string_view data(
          reinterpret_cast<char*>(_encode_buffer.data()), _encode_buffer.size()
      );
      scope->WriteEvent(data, {});
    }
  }
};

/**
 * Result of processing a single RumCommand in a single RumScope.
 */
enum class RumScopeResult : uint8_t {
  /** Command processed; scope should be left open. */
  RemainOpen,
  /** Command processed; scope should be closed. */
  Close
};

/**
 * `RumScope` describes any type that defines the following member function:
 *
 * - RumScopeResult Process(const RumCommand& command);
 *
 * RUM models application state as a hierarchy of RUM "scopes," rooted at the
 * `RumApplicationScope` and proceeding with increasing granularity toward leaf nodes
 * like `RumResourceScope` and `RumActionScope`. At any given time, there is:
 *
 * - Exactly 1 `RumApplicationScope`
 *   - Either 0 or 1 active `RumSessionScope`, potentially overlapping with the
 *     previous session once it's no longer active
 *     - Either 0 or 1 active `RumViewScope`, with any number in existence
 *       - Any number of `RumActionScope` belonging to each view
 *       - Any number of `RumResourceScope` belonging to each view
 *
 * Each of these types implements `RumScope`.
 *
 * When application state changes in a way that's relevant to RUM, a `RumCommand` is
 * generated and propagated to all relevant scopes via the `Process` function. When a
 * Scope processes a command, it returns either `RumScopeResult::RemainOpen` or
 * `RumScopeResult::Close` to indicate whether that event should close the scope.
 */
struct RumScope {};

/**
 * Wrapper for any number of scopes of a consistent type. Used within a given scope type
 * to own child scopes and propagate commands to them.
 */
template <typename T>
struct ScopeArray {
  /**
   * List of owned scopes still active: once a scope returns `RumScopeResult::Close`, it
   * will be removed from this list.
   */
  std::vector<T> items;

  ScopeArray() {
    // TODO(RUM-12946): Preallocating space for up to 8 items masks a potential
    // use-after-free bug: this is a band-aid.
    //
    // To reproduce: remove the reserve() call below, then run any test that calls
    // StartView(), StartAction(), and then StartView(). ASan will catch the bug during
    // `RumActionScope::SendActionEvent`, at the point where the action scope is
    // attempting to cal functions on its parent view scope using an address where that
    // RumViewScope no longer resides.
    //
    // The problem boils down to this:
    // - RumSessionScope holds a ScopeArray<RumViewScope>, which holds view scopes
    //   by value in a std::vector
    // - RumActionScope is initialized with a reference to its parent RumViewScope
    // - This isn't actually safe: when we add a new view scope, the vector may be
    //   resized, causing existing RumViewScope values to be moved, and invalidating
    //   the reference stored by RumActionScope
    //
    items.reserve(8);
  }

  /**
   * Creates a new scope and adds it to the end of the array.
   */
  template <typename... Args>
  T& Push(Args&&... args) {
    return items.emplace_back(std::forward<Args>(args)...);
  }

  /**
   * Propagates the given command to all scopes in the array, calling the `Process()`
   * function on each scope. Any scopes that return `RumScopeResult::Close` will be
   * removed from the array.
   */
  void Propagate(const RumCommand& command) {
    // Call Process() on all scopes, and if any return 'Close', shuffle them to the end
    // of the vector
    auto new_end = std::remove_if(items.begin(), items.end(), [&](T& scope) {
      RumScopeResult result = scope.Process(command);
      return result == RumScopeResult::Close;
    });

    // Remove any newly-closed scopes from the vector
    items.erase(new_end, items.end());
  }
};

/**
 * Non-owning, nullable reference to a single child scope.
 */
template <typename T>
using ScopeRef = std::optional<std::reference_wrapper<T>>;

}  // namespace datadog::impl
