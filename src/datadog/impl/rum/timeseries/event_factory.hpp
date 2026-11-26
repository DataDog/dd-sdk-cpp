// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "datadog/impl/core/context.hpp"
#include "datadog/impl/rum/timeseries/bound_session.hpp"
#include "datadog/impl/rum/timeseries/data_point.hpp"

namespace datadog::impl {

/**
 * Builds the event for one batch of data points and encodes it as JSON.
 *
 * Threading: Encode() is only called on the context thread. The destructor may run on
 * any thread (e.g. the scheduler thread or the thread that releases the last reference
 * to the pipeline), so it must not assume a thread.
 *
 * Contracts:
 *  - Encode() must not throw: the SDK may be built with `-fno-exceptions`.
 *  - Timestamps are passed through exactly as the reader produced them. They come from
 *    the wall clock, so they may be non-monotonic or have gaps; do not assume equal
 *    spacing between points.
 */
template <typename T>
class EventFactory {
 public:
  virtual ~EventFactory() = default;

  /**
   * Builds and encodes the event for `points`. Every session-scoped value comes from
   * `session` (the one the pipeline is bound to), never from `ctx.rum`, which may
   * already describe a different session by the time the batch is written. `ctx`
   * provides only SDK-wide values (service, OS, device, user...).
   *
   * @returns The encoded JSON, which stays valid until the next call to Encode(), or
   *  std::nullopt if the event could not be built or encoded. An empty view is treated
   *  as a failure too, since empty events cannot be written.
   */
  virtual std::optional<std::string_view> Encode(
      const CoreContext& ctx,
      const TimeseriesBoundSession& session,
      const std::vector<DataPoint<T>>& points
  ) = 0;

 protected:
  EventFactory() = default;

  // Polymorphic base: copying/moving through a base reference would slice
  EventFactory(const EventFactory&) = delete;
  EventFactory& operator=(const EventFactory&) = delete;
  EventFactory(EventFactory&&) = delete;
  EventFactory& operator=(EventFactory&&) = delete;
};

}  // namespace datadog::impl
