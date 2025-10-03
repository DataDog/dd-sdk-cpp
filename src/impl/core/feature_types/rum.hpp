// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cinttypes>
#include <string_view>

#include "datadog/uuid.hpp"

namespace datadog::impl {

/**
 * Additional context for the RUM feature that's made accessible to other features via
 * CoreContext.
 *
 * Other features can read the current RUM feature context in order to access relevant
 * state like the current session ID. This allows those other features to enrich their
 * event payloads with RUM data, which facilitates correlation in the backend.
 */
struct RumFeatureContext {
  UUID application_id;  // UUID::Zero if RUM not initialized
  UUID session_id;      // UUID::Zero if no active session
  UUID view_id;         // UUID::Zero if no active view
  UUID action_id;       // UUID::Zero if no active action
};

}  // namespace datadog::impl
