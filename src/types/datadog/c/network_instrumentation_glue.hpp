// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2026-Present Datadog, Inc.

#pragma once

#include <utility>
#include <vector>

#include "datadog/network_instrumentation.h"
#include "datadog/network_instrumentation.hpp"

struct dd_http_headers {
  const std::vector<datadog::HttpHeader> headers;

  explicit dd_http_headers(std::vector<datadog::HttpHeader>&& in_headers)
      : headers(std::move(in_headers)) {}
};
