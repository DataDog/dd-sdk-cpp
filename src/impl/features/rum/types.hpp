// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <string_view>

#include "datadog/rum.h"
#include "datadog/rum.hpp"

namespace datadog {

inline RumConfig RumConfig_FromC(const dd_rum_config_t& config) {
  // Convert from dd_uuid_t to datadog::UUID
  const uint8_t* bytes = static_cast<const uint8_t*>(config.application_id.bytes);
  const UUID application_id(bytes);

  // Initialize a C++ config struct from our input values
  return RumConfig(application_id).SetSessionSampleRate(config.session_sample_rate);
}

inline RumActionType RumActionType_FromC(dd_rum_action_type_t value) {
  static_assert(static_cast<int>(RumActionType::Tap) == DD_RUM_ACTION_TYPE_TAP);
  static_assert(static_cast<int>(RumActionType::Click) == DD_RUM_ACTION_TYPE_CLICK);
  static_assert(static_cast<int>(RumActionType::Scroll) == DD_RUM_ACTION_TYPE_SCROLL);
  static_assert(static_cast<int>(RumActionType::Swipe) == DD_RUM_ACTION_TYPE_SWIPE);
  static_assert(static_cast<int>(RumActionType::Custom) == DD_RUM_ACTION_TYPE_CUSTOM);
  return static_cast<RumActionType>(value);
}

}  // namespace datadog
