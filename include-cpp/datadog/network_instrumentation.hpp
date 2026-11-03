// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cinttypes>
#include <string>
#include <type_traits>
#include <vector>

#include "datadog/api.hpp"

namespace datadog {

// Forward declarations
namespace impl {
class FirstPartyHostsMatcher;
}  // namespace impl

/**
 * Types of HTTP headers used for trace context injection.
 *
 * See
 * https://docs.datadoghq.com/tracing/other_telemetry/rum/?tab=browserrum#opentelemetry-support
 */
enum class TracingHeaderTypes : uint8_t {
  /** Datadog's propagator header type. */
  Datadog = (1 << 0),
  /**
   * Open Telemetry B3 single header type.
   *
   * See: https://github.com/openzipkin/b3-propagation#single-header
   */
  B3 = (1 << 1),
  /**
   * Open Telemetry B3 multiple headers type.
   *
   * See: https://github.com/openzipkin/b3-propagation#multiple-headers
   */
  B3multi = (1 << 2),
  /**
   * W3C trace context header type.
   *
   * See: https://www.w3.org/TR/trace-context/
   */
  Tracecontext = (1 << 3),
  /**
   * Default header types (Datadog + W3C trace context).
   */
  Default = (1 << 0) | (1 << 3)
};

/**
 * Returns an union of the two given `TracingHeaderTypes`.
 */
constexpr TracingHeaderTypes operator|(TracingHeaderTypes lhs, TracingHeaderTypes rhs) {
  return static_cast<TracingHeaderTypes>(
      static_cast<std::underlying_type_t<TracingHeaderTypes>>(lhs) |
      static_cast<std::underlying_type_t<TracingHeaderTypes>>(rhs)
  );
}

/**
 * A host for which requests should propagate tracing information using
 * HTTP headers defined by one or more `TracingHeaderTypes`.
 */
struct FirstPartyHost {
  /** The host name. Do not include the URL schema (https://, etc) nor a port. */
  std::string host;

  /**
   * The required header types. Can be a value of `TracingHeaderTypes` or
   * an union of them.
   */
  TracingHeaderTypes header_types;
};

/**
 * Holds one or more `FirstPartyHost` representing the hosts for which requests
 * may have trace propagation headers.
 *
 * A configured host will match any hostname that is either equal to itself,
 * and any subdomain. As an example, a configured host `example.com` matches itself,
 * but also `store.example.com` or `cds.example.com`. Note the matching is based on
 * subdomains, not just string prefix. This means the same configured host
 * `example.com` does _not_ match `myexample.com`, but matches `my.example.com`.
 *
 * If a given host matches multiple `FirstPartyHost` entries in a `FirstPartyHosts`,
 * the first match will be used to decide which header types may be included. This
 * means more specific hosts should come before more generic ones. As an example,
 * suppose `store.example.com` should propagate B3 single headers, while all other
 * hosts under `example.com` should use the default headers. The resulting
 * configuration would be:
 *
 * const FirstPartyHosts hosts{{
 *        {"store.example.com", TracingHeaderTypes::B3},
 *        {"example.com", TracingHeaderTypes.Default}
 *      }};
 *
 * If the order of the hosts is inverted, `store.example.com` will match the more
 * generic rule first, and use the default header types.
 */
struct FirstPartyHosts {
  /**
   * Creates a `FirstPartyHosts` from the given vector of `FirstPartyHost`.
   *
   * Copies all host entries and their strings. The input vector does not need to
   * outlive this object.
   */
  DATADOG_API explicit FirstPartyHosts(const std::vector<FirstPartyHost>& in_hosts);

  /**
   * Creates a `FirstPartyHosts` from a list of hosts.
   *
   * All hosts will use the default tracing header types. Provide only the hostnames,
   * without including the URL schema (https://, etc) nor a port.
   *
   * Copies all host entries and their strings. The input vector does not need to
   * outlive this object.
   */
  DATADOG_API explicit FirstPartyHosts(const std::vector<std::string>& in_hosts);

  FirstPartyHosts(const FirstPartyHosts&) = default;
  FirstPartyHosts(FirstPartyHosts&&) = default;
  FirstPartyHosts& operator=(const FirstPartyHosts&) = default;
  FirstPartyHosts& operator=(FirstPartyHosts&&) = default;

 private:
  std::vector<FirstPartyHost> _hosts;

  friend class impl::FirstPartyHostsMatcher;
};

}  // namespace datadog
