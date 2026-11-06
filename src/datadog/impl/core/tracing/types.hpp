// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <vector>

#include "datadog/network_instrumentation.h"
#include "datadog/network_instrumentation.hpp"

namespace datadog {

inline TracingHeaderTypes TracingHeaderTypes_FromC(dd_tracing_header_types_t value) {
  static_assert(
      static_cast<int>(TracingHeaderTypes::Datadog) == DD_TRACING_HEADER_TYPES_DATADOG
  );
  static_assert(static_cast<int>(TracingHeaderTypes::B3) == DD_TRACING_HEADER_TYPES_B3);
  static_assert(
      static_cast<int>(TracingHeaderTypes::B3multi) == DD_TRACING_HEADER_TYPES_B3_MULTI
  );
  static_assert(
      static_cast<int>(TracingHeaderTypes::Tracecontext) ==
      DD_TRACING_HEADER_TYPES_TRACE_CONTEXT
  );
  static_assert(
      static_cast<int>(TracingHeaderTypes::Default) == DD_TRACING_HEADER_TYPES_DEFAULT
  );
  return static_cast<TracingHeaderTypes>(value);
}

inline FirstPartyHosts FirstPartyHosts_FromC(const dd_first_party_hosts_t& value) {
  std::vector<FirstPartyHost> hosts;
  if (!value.entries) {
    return FirstPartyHosts(hosts);
  }

  hosts.reserve(value.count);
  for (size_t i = 0; i < value.count; ++i) {
    const dd_first_party_host_t& host = value.entries[i];
    hosts.push_back(
        {host.host ? host.host : "", TracingHeaderTypes_FromC(host.header_types)}
    );
  }
  return FirstPartyHosts(hosts);
}

}  // namespace datadog
