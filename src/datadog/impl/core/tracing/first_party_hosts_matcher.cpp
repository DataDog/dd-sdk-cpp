// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "first_party_hosts_matcher.hpp"

#include <algorithm>

namespace datadog::impl {

namespace {

constexpr char ToAsciiLower(char character) {
  if (character >= 'A' && character <= 'Z') {
    return static_cast<char>(character + 32);
  }

  return character;
}

constexpr bool AsciiCaseInsensitiveEqual(char lhs, char rhs) {
  return ToAsciiLower(lhs) == ToAsciiLower(rhs);
}

}  // namespace

std::optional<TracingHeaderTypes> FirstPartyHostsMatcher::MatchesHost(
    const FirstPartyHosts& first_party_hosts, std::string_view host
) {
  for (const auto& [configured_host, header_types] : first_party_hosts._hosts) {
    if (MatchesHost(configured_host, host)) {
      return header_types;
    }
  }

  return std::nullopt;
}

bool FirstPartyHostsMatcher::MatchesHost(
    std::string_view configured_host, std::string_view host
) {
  if (configured_host.empty() || host.size() < configured_host.size()) {
    return false;
  }

  const auto suffix_offset = host.size() - configured_host.size();
  const auto suffix = host.substr(suffix_offset);
  if (!std::equal(
          configured_host.begin(),
          configured_host.end(),
          suffix.begin(),
          AsciiCaseInsensitiveEqual
      )) {
    return false;
  }

  return suffix_offset == 0 || host[suffix_offset - 1] == '.';
}

}  // namespace datadog::impl
