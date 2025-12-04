// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <string_view>

#include "datadog/rum.hpp"

namespace datadog::impl {

/**
 * Details of an error reported to RUM by the application.
 */
struct RumErrorDetails {
  std::string_view name;
  std::string_view type;
  std::string_view stack_trace;
  bool is_network_error{false};
};

/**
 * Details of an HTTP request used to initialize a RUM Resource.
 */
struct RumRequestDetails {
  RumResourceMethod method;
  std::string_view url;
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
