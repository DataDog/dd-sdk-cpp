// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/rum/timeseries/cpu_event_factory.hpp"

#include <cmath>
#include <utility>

#include "datadog/timestamp.hpp"
#include "datadog/uuid.hpp"

#include "datadog/impl/rum/scopes/event_enrichment.hpp"
#include "datadog/impl/types/events/timestamp.hpp"
#include "datadog/impl/types/json.hpp"
#include "datadog/impl/types/rum.hpp"

namespace datadog::impl {

std::optional<std::string_view> CpuEventFactory::Encode(
    const CoreContext& ctx,
    const TimeseriesBoundSession& session,
    const std::vector<DataPoint<double>>& points
) {
  // Keep the arrays aligned index-for-index, without the values the schema rejects
  std::vector<NanoTimestamp> timestamps;
  std::vector<double> values;
  timestamps.reserve(points.size());
  values.reserve(points.size());
  for (const DataPoint<double>& point : points) {
    if (!std::isfinite(point.value)) {
      continue;
    }
    timestamps.push_back(point.timestamp);
    values.push_back(point.value);
  }
  if (values.empty()) {
    return std::nullopt;
  }

  const Timestamp start = timestamps.front().value;
  const Timestamp end = timestamps.back().value;
  RumTimeseriesCpuEvent ev{
      start,
      session.application_id,
      session.session_id,
      session.type,
      UUID::Random(),
      start,
      end,
  };
  ev.timeseries.data.timestamps = std::move(timestamps);
  ev.timeseries.data.values.cpu_usage = std::move(values);

  // Session-scoped values come from the bound session only: ctx.rum may already
  // describe a newer session
  RumTimeseriesCpuEvent::Internal::Session dd_session;
  dd_session.session_precondition = session.precondition;
  ev._dd.session.value = dd_session;
  ev._dd.configuration.value =
      RumTimeseriesCpuEvent::Internal::Configuration(session.session_sample_rate);

  // SDK-wide values come from ctx
  RumEventEnrichment::PopulateSystemProperties(ctx, ev);

  EncodeJson(_encode_buffer, ev);
  return std::string_view(
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
      reinterpret_cast<const char*>(_encode_buffer.data()),
      _encode_buffer.size()
  );
}

}  // namespace datadog::impl
