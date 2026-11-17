// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/network_instrumentation.hpp"

namespace datadog {

FirstPartyHosts::FirstPartyHosts(const std::vector<FirstPartyHost>& in_hosts)
    : _hosts(in_hosts) {}

FirstPartyHosts::FirstPartyHosts(const std::vector<std::string>& in_hosts) {
  _hosts.reserve(in_hosts.size());

  for (const auto& host : in_hosts) {
    _hosts.push_back({host, TracingHeaderTypes::Default});
  }
}

}  // namespace datadog
