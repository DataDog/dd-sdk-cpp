// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <string>
#include <string_view>
#include <utility>

#include "datadog/network_instrumentation.hpp"

namespace datadog::impl {

class HttpHeaderFactory {
 public:
  HttpHeaderFactory() = delete;

  static HttpHeader Create(std::string_view name, std::string value) {
    return HttpHeader{name, std::move(value)};
  }
};

}  // namespace datadog::impl
