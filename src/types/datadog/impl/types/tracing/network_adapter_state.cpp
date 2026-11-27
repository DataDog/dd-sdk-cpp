// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2026-Present Datadog, Inc.

#include "datadog/impl/types/tracing/network_adapter_state.hpp"

#include <mutex>
#include <utility>

namespace datadog::impl {

NetworkAdapterState::NetworkAdapterState(
    std::shared_ptr<SamplingDecisionProvider> sampling_decision_provider,
    bool is_running,
    TrackingConsent tracking_consent,
    std::string user_id,
    std::string account_id
)
    : sampling_decision_provider(std::move(sampling_decision_provider)),
      _is_running(is_running),
      _tracking_consent(tracking_consent),
      _user_id(std::move(user_id)),
      _account_id(std::move(account_id)) {}

void NetworkAdapterState::SetIsRunning(bool is_running) {
  std::lock_guard lock(_mutex);
  _is_running = is_running;
}

void NetworkAdapterState::SetTrackingConsent(TrackingConsent tracking_consent) {
  std::lock_guard lock(_mutex);
  _tracking_consent = tracking_consent;
}

void NetworkAdapterState::SetUserId(std::string_view user_id) {
  std::lock_guard lock(_mutex);
  _user_id = user_id;
}

void NetworkAdapterState::SetAccountId(std::string_view account_id) {
  std::lock_guard lock(_mutex);
  _account_id = account_id;
}

}  // namespace datadog::impl
