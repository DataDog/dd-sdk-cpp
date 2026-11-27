// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2026-Present Datadog, Inc.

#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#include "datadog/core.hpp"

#include "datadog/impl/types/tracing/sampling_decision_provider.hpp"

namespace datadog::impl {

class NetworkAdapterState {
 public:
  explicit NetworkAdapterState(
      std::shared_ptr<SamplingDecisionProvider> sampling_decision_provider,
      bool is_running,
      TrackingConsent tracking_consent,
      std::string user_id,
      std::string account_id
  );

  void SetIsRunning(bool is_running);
  void SetTrackingConsent(TrackingConsent tracking_consent);
  void SetUserId(std::string_view user_id);
  void SetAccountId(std::string_view account_id);

  const std::shared_ptr<SamplingDecisionProvider> sampling_decision_provider;

 private:
  std::mutex _mutex;
  bool _is_running;
  TrackingConsent _tracking_consent;
  std::string _user_id;
  std::string _account_id;
};

}  // namespace datadog::impl
