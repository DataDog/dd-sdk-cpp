// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include "datadog/uuid.hpp"

#include "datadog/impl/types/rum.hpp"

namespace datadog::impl {

/**
 * The RUM session that a set of timeseries pipelines is bound to. Fixed when the
 * pipelines are created and used for every event they write, regardless of what
 * `CoreContext::rum` says at write time (which can already describe a newer session).
 *
 * Holds every session-scoped value a timeseries event needs. Build it from the
 * session's RumContext: RumFeatureContext lacks the precondition.
 */
struct TimeseriesBoundSession final {
  /** Every value is required, so none can be forgotten when the session is built. */
  TimeseriesBoundSession(
      const UUID& in_application_id,
      const UUID& in_session_id,
      RumSessionType in_type,
      RumSessionPrecondition in_precondition,
      float in_session_sample_rate
  )
      : application_id(in_application_id),
        session_id(in_session_id),
        type(in_type),
        precondition(in_precondition),
        session_sample_rate(in_session_sample_rate) {}

  UUID application_id;
  UUID session_id;
  RumSessionType type;
  /** Why the session started; serialized as `_dd.session.session_precondition`. */
  RumSessionPrecondition precondition;
  /**
   * RumConfig::session_sample_rate in [0, 100] when the session started, serialized
   * as `_dd.configuration.session_sample_rate`.
   */
  float session_sample_rate;
};

}  // namespace datadog::impl
