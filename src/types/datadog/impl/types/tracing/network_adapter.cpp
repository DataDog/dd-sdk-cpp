// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/types/tracing/network_adapter.hpp"

#include <utility>

namespace datadog::impl {

NetworkAdapter::NetworkAdapter(
    TraceInjectionPolicy policy,
    float sample_rate,
    std::optional<FirstPartyHosts> custom_first_party_hosts,
    std::weak_ptr<NetworkAdapterState> state,
    DiagnosticLogger logger
)
    : _policy(policy),
      _sample_rate(sample_rate),
      _custom_first_party_hosts(std::move(custom_first_party_hosts)),
      _state(std::move(state)),
      _logger(std::move(logger)) {
  (void)_policy;
  (void)_sample_rate;
}

}  // namespace datadog::impl
