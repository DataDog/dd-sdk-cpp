// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cstdint>
#include <vector>

#include "datadog/impl/core/block.hpp"
#include "datadog/impl/core/feature_scope.hpp"

/**
 * Creates an EventWriter for a test that does not care whether an event arrives by
 * WriteCopy() or WriteMove(). Both deliver the event to `sink` as a `Block`: for a
 * moved vector, that is a view of its bytes, which stay valid during the call. The
 * default sink accepts every event and does nothing with it.
 */
inline datadog::impl::EventWriter MakeTestEventWriter(
    datadog::impl::EventWriter::CopyingFunc sink =
        [](datadog::impl::Block, datadog::impl::Block, bool) { return true; }
) {
  return datadog::impl::EventWriter(
      sink,
      [sink](
          std::vector<uint8_t>&& event,
          datadog::impl::Block event_metadata,
          bool bypass_tracking_consent
      ) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        const datadog::impl::Block view(
            reinterpret_cast<const char*>(event.data()), event.size()
        );
        return sink(view, event_metadata, bypass_tracking_consent);
      }
  );
}
