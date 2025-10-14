// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "core/feature_types/rum.hpp"

#include "assert.hpp"

namespace datadog::impl {

std::string_view RumSessionPrecondition_ToString(RumSessionPrecondition value) {
  switch (value) {
    case RumSessionPrecondition::UserAppLaunch:
      return "user_app_launch";
    case RumSessionPrecondition::InactivityTimeout:
      return "inactivity_timeout";
    case RumSessionPrecondition::MaxDuration:
      return "max_duration";
    case RumSessionPrecondition::BackgroundLaunch:
      return "background_launch";
    case RumSessionPrecondition::Prewarm:
      return "prewarm";
    case RumSessionPrecondition::FromNonInteractiveSession:
      return "from_non_interactive_session";
    case RumSessionPrecondition::ExplicitStop:
      return "explicit_stop";
  }
  DATADOG_ASSERT(false, "invalid RumSessionPrecondition value");
  return "unknown_precondition";
}

}  // namespace datadog::impl
