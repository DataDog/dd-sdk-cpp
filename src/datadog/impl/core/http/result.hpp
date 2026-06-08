// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cinttypes>

namespace datadog::impl {

enum class HttpResultType : uint8_t {
  /**
   * We were unable to initiate the request due to an internal error in our HTTP
   * implementation (e.g. failed allocation, bad invariants), indicating a fatal error
   * state.
   */
  SentNoRequest,
  /**
   * We failed to complete the request, and the request should be discarded and never
   * retried.
   */
  GotNoResponse_NonRetryable,
  /**
   * We failed to complete the request due to transient network conditions, but the same
   * request may succeed if retried later.
   */
  GotNoResponse_Retryable,
  /**
   * We completed the request and got a valid response, and status_code is set.
   */
  GotResponse,
};

/**
 * Result of an attempt to send an HTTP request. status_code is only set if type is
 * GotResponse.
 */
struct HttpResult {
  HttpResultType type;
  int status_code;
};

}  // namespace datadog::impl
