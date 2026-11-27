// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <memory>
#include <optional>

#include "datadog/network_instrumentation.hpp"

#include "datadog/impl/types/diagnostics.hpp"

namespace datadog::impl {

class NetworkAdapterState;

class NetworkAdapter {
 public:
  NetworkAdapter(
      TraceInjectionPolicy policy,
      float sample_rate,
      std::optional<FirstPartyHosts> custom_first_party_hosts,
      std::weak_ptr<NetworkAdapterState> state,
      DiagnosticLogger logger
  );

 private:
  const TraceInjectionPolicy _policy;
  const float _sample_rate;
  const std::optional<FirstPartyHosts> _custom_first_party_hosts;
  const std::weak_ptr<NetworkAdapterState> _state;
  const DiagnosticLogger _logger;
};

}  // namespace datadog::impl
