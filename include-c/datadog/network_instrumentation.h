// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#ifndef DATADOG_INCLUDE_NETWORK_INSTRUMENTATION_H
#define DATADOG_INCLUDE_NETWORK_INSTRUMENTATION_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Types of HTTP headers used for trace context injection.
 *
 * See
 * https://docs.datadoghq.com/tracing/other_telemetry/rum/?tab=browserrum#opentelemetry-support
 */
typedef enum {
  /** Datadog's propagator header type. */
  DD_TRACING_HEADER_TYPES_DATADOG = 1U << 0,
  /**
   * Open Telemetry B3 single header type.
   *
   * See: https://github.com/openzipkin/b3-propagation#single-header
   */
  DD_TRACING_HEADER_TYPES_B3 = 1U << 1,
  /**
   * Open Telemetry B3 multiple headers type.
   *
   * See: https://github.com/openzipkin/b3-propagation#multiple-headers
   */
  DD_TRACING_HEADER_TYPES_B3_MULTI = 1U << 2,
  /**
   * W3C trace context header type.
   *
   * See: https://www.w3.org/TR/trace-context/
   */
  DD_TRACING_HEADER_TYPES_TRACE_CONTEXT = 1U << 3,
  /**
   * Default header types (Datadog + W3C trace context).
   */
  DD_TRACING_HEADER_TYPES_DEFAULT =
      DD_TRACING_HEADER_TYPES_DATADOG | DD_TRACING_HEADER_TYPES_TRACE_CONTEXT
} dd_tracing_header_types_t;

/**
 * A host for which requests should propagate tracing information using
 * HTTP headers defined by one or more `dd_tracing_header_types_t`.
 */
typedef struct dd_first_party_host {
  /** The host name. Do not include the URL schema (https://, etc) nor a port. */
  const char* host;
  /**
   * The required header types. Can be a value of `dd_tracing_header_types_t` or
   * an union of them.
   */
  dd_tracing_header_types_t header_types;
} dd_first_party_host_t;

/**
 * Holds one or more `dd_first_party_host_t` representing the hosts for which requests
 * may have trace propagation headers.
 *
 * A configured host will match any hostname that is either equal to itself,
 * and any subdomain. As an example, a configured host `example.com` matches itself,
 * but also `store.example.com` or `cds.example.com`. Note the matching is based on
 * subdomains, not just string prefix. This means the same configured host
 * `example.com` does _not_ match `myexample.com`, but matches `my.example.com`.
 *
 * If a given host matches multiple `dd_first_party_host_t` entries in a
 * `dd_first_party_hosts_t`, the first match will be used to decide which header types
 * may be included. This means more specific hosts should come before more generic ones.
 * As an example, suppose `store.example.com` should propagate B3 single headers, while
 * all other hosts under `example.com` should use the default headers. The resulting
 * configuration would be:
 *
 * const dd_first_party_host_t entries[] = {
 *     {"store.example.com", DD_TRACING_HEADER_TYPES_B3},
 *     {"example.com", DD_TRACING_HEADER_TYPES_DEFAULT}
 * };
 * const dd_first_party_hosts_t hosts = {entries, 2};
 *
 * If the order of the hosts is inverted, `store.example.com` will match the more
 * generic rule first, and use the default header types.
 */
typedef struct dd_first_party_hosts {
  /**
   * Array of `dd_first_party_host_t` as described above.
   */
  const dd_first_party_host_t* entries;
  /**
   * The number of entries in `entries`.
   */
  size_t count;
} dd_first_party_hosts_t;

#ifdef __cplusplus
}
#endif

#endif  // DATADOG_INCLUDE_NETWORK_INSTRUMENTATION_H
