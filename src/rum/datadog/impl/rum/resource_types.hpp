// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <string>

#include "datadog/rum.hpp"

namespace datadog::impl {

/**
 * Details of an error reported to RUM by the application.
 */
struct RumErrorDetails {
  std::string message;
  std::string type;
  std::string stack_trace;
  bool is_network_error{false};

  RumErrorDetails() = default;

  explicit RumErrorDetails(
      std::string_view in_message,
      std::string_view in_type = "",
      std::string_view in_stack_trace = "",
      bool in_is_network_error = false
  )
      : message(in_message),
        type(in_type),
        stack_trace(in_stack_trace),
        is_network_error(in_is_network_error) {}
};

/**
 * Details of an HTTP request used to initialize a RUM Resource.
 */
struct RumRequestDetails {
  RumResourceMethod method{RumResourceMethod::Get};
  std::string url;

  RumRequestDetails() = default;

  explicit RumRequestDetails(RumResourceMethod in_method, std::string_view in_url)
      : method(in_method), url(in_url) {}
};

/**
 * Details of an HTTP response used to finalize a RUM Resource in conjunction with any
 * applicable error details. If status_code is 0, no response was received.
 */
struct RumResponseDetails {
  int32_t status_code{0};
  int64_t size{-1};
  RumResourceType resource_type{RumResourceType::Unknown};
};

}  // namespace datadog::impl
