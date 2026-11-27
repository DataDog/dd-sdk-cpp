// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "datadog/impl/core/context.hpp"
#include "datadog/impl/rum/timeseries/bound_session.hpp"
#include "datadog/impl/rum/timeseries/data_point.hpp"
#include "datadog/impl/rum/timeseries/event_factory.hpp"

namespace datadog::impl {

/**
 * Encodes a batch of CPU usage samples as one RumTimeseriesCpuEvent.
 *
 * Non-finite values are dropped with their timestamps (they would be written as
 * `null`, which the schema rejects); a batch with no value left is not encoded.
 * `timeseries.start` and `timeseries.end` are the first and last remaining
 * timestamps, and `date` is the start of the batch. Each event gets a new random
 * `timeseries.id`.
 */
class CpuEventFactory final : public EventFactory<double> {
 public:
  CpuEventFactory() = default;

  std::optional<std::string_view> Encode(
      const CoreContext& ctx,
      const TimeseriesBoundSession& session,
      const std::vector<DataPoint<double>>& points
  ) override;

 private:
  std::vector<uint8_t> _encode_buffer;
};

}  // namespace datadog::impl
