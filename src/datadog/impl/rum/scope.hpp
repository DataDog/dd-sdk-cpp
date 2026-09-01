// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <algorithm>
#include <cinttypes>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "datadog/impl/core/feature_scope.hpp"
#include "datadog/impl/core/platform/clock.hpp"
#include "datadog/impl/core/util/diagnostics.hpp"
#include "datadog/impl/rum/command.hpp"

namespace datadog {
struct RumConfig;
}

namespace datadog::impl {

/**
 * Given a raw 64-bit `seed` and a `sample_rate` in [0, 100]: applies Knuth
 * multiplicative hashing and compares the result against a threshold scaled from
 * `sample_rate`. Returns true if the session should be sampled in.
 *
 * Exposed for direct testing with seed values used in other SDKs' test suites.
 * Production code goes through `RumScopeDependencies::ShouldSampleSession`.
 */
bool ShouldSampleSessionFromSeed(uint64_t seed, float sample_rate);

/**
 * Immutable set of input values used during RumScope processing.
 */
struct RumScopeDependencies {
  // Configuration details
  UUID application_id;
  DiagnosticLogger diagnostic_logger;
  const platform::IClock& clock;
  bool track_anonymous_user;  // From RumConfig::track_anonymous_user

 private:
  float _sampling_rate;  // [0.0f..100.f], from RumConfig::session_sample_rate

  // Reusable buffer for encoding events; accessed only on the context thread
  mutable std::vector<uint8_t> _encode_buffer;

 public:
  explicit RumScopeDependencies(
      const RumConfig& config, const platform::IClock& in_clock
  );

 public:
  /**
   * Determines whether the session identified by `session_id` should be sampled in,
   * using the deterministic Knuth multiplicative hash algorithm used by all RUM SDKs.
   *
   * If true, RUM will process the full set of commands for this session and generate
   * RUM events to be sent to intake. If false, the session is not sampled, meaning the
   * session scope ignores most commands and generate no events.
   */
  bool ShouldSampleSession(
      const UUID& session_id, std::optional<float> sample_rate = std::nullopt
  ) const;

  /** Returns the configured session sample rate in [0.0, 100.0]. */
  float GetSessionSampleRate() const { return _sampling_rate; }

  /**
   * Encodes a RUM event to JSON without writing it. The caller uses the returned
   * string_view with the EventWriter callback to actually write the event.
   *
   * @param event RUM event to encode
   * @return JSON-encoded event as a string_view referencing the internal encode buffer.
   *  Valid until the next call to EncodeEvent().
   */
  template <typename T>
  std::string_view EncodeEvent(const T& event) const {
    EncodeJson(_encode_buffer, event);
    return std::string_view(
        reinterpret_cast<const char*>(_encode_buffer.data()), _encode_buffer.size()
    );
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
 * Non-owning, nullable reference to a single child scope.
 */
template <typename T>
using ScopeRef = std::optional<std::reference_wrapper<T>>;

}  // namespace datadog::impl
