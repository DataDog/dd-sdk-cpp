// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/rum/context.hpp"

namespace datadog::impl {

void RumContext::Reset() {
  application_id = UUID::Zero;

  session_id = UUID::Zero;
  session_is_active = false;
  session_is_sampled = false;

  active_view_id = UUID::Zero;
  active_view_key.clear();
  active_view_name.clear();

  active_action_id = UUID::Zero;
}

RumFeatureContext RumContext::ToFeatureContext() const {
  // If we don't have a valid RUM Application ID, return a zero-initialized value
  if (application_id == UUID::Zero) {
    return RumFeatureContext{};
  }

  // If we don't have an active session, or if the session isn't sampled, populate the
  // application ID but leave it at that
  if (session_id == UUID::Zero || !session_is_active || !session_is_sampled) {
    return RumFeatureContext{application_id, UUID::Zero, UUID::Zero, UUID::Zero, {}};
  }

  // We have a valid session: if there's no active view, just set application and
  // session ID
  if (active_view_id == UUID::Zero) {
    return RumFeatureContext{application_id, session_id, UUID::Zero, UUID::Zero, {}};
  }

  // Determine view name: prefer explicit name, fall back to key
  std::string view_name_str =
      active_view_name.empty() ? active_view_key : active_view_name;

  // We have an active view: if there's no active action, set application, session, and
  // view state
  if (active_action_id == UUID::Zero) {
    return RumFeatureContext{
        application_id, session_id, active_view_id, UUID::Zero, std::move(view_name_str)
    };
  }

  // We have an active action
  return RumFeatureContext{
      application_id,
      session_id,
      active_view_id,
      active_action_id,
      std::move(view_name_str)
  };
}

}  // namespace datadog::impl
