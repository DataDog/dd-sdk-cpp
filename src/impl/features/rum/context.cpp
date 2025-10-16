// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "features/rum/context.hpp"

namespace datadog::impl {

RumFeatureContext RumContext::ToFeatureContext() const {
  // If we don't have a valid RUM Application ID, return a zero-initialized value
  if (application_id == UUID::Zero) {
    return RumFeatureContext{};
  }

  // If we don't have an active session, or if the session isn't sampled, populate the
  // application ID but leave it at that
  if (session_id == UUID::Zero || !session_is_active || !session_is_sampled) {
    return RumFeatureContext{application_id, UUID::Zero, UUID::Zero, UUID::Zero};
  }

  // We have a valid session
  // TODO(RUM-11368): Set view_id if our session has an active view
  // TODO(RUM-11369): Set action_id if that view has an active action
  return RumFeatureContext{application_id, session_id, UUID::Zero, UUID::Zero};
}

}  // namespace datadog::impl
