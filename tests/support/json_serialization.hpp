// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cinttypes>
#include <string_view>
#include <vector>

#include "json.hpp"

using namespace datadog::impl;

/**
 * Given any JSON-serializable value T, calls `EncodeJson` to verify that it is
 * formatted as expected when converted to JSON.
 */
template <typename T>
void RequireJsonValue(const T& value, std::string_view want) {
  std::vector<uint8_t> buffer;
  EncodeJson<T>(buffer, value);

  std::string_view got(reinterpret_cast<const char*>(buffer.data()), buffer.size());
  REQUIRE(got == want);
}
