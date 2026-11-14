// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "first_party_hosts_matcher.hpp"

#include <algorithm>

namespace datadog::impl {

static constexpr char to_ascii_lower(char character) {
  if (character >= 'A' && character <= 'Z') {
    return static_cast<char>(character + 32);
  }

  return character;
}

static constexpr bool ascii_case_insensitive_equal(char lhs, char rhs) {
  return to_ascii_lower(lhs) == to_ascii_lower(rhs);
}

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
  // Return early in two situations: the configured host is empty (invalid)
  // and would cause this function to erroneously return true if `host` is
  // an absolute DNS name (trailing dot), or the host being tested is smaller
  // than the configured_host, meaning it can't possibly be a match, and
  // avoiding suffix_outset below to become negative.
  if (configured_host.empty() || host.size() < configured_host.size()) {
    return false;
  }

  const auto suffix_offset = host.size() - configured_host.size();
  const auto suffix = host.substr(suffix_offset);
  if (!std::equal(
          configured_host.begin(),
          configured_host.end(),
          suffix.begin(),
          ascii_case_insensitive_equal
      )) {
    return false;
  }

  return suffix_offset == 0 || host[suffix_offset - 1] == '.';
}

}  // namespace datadog::impl
