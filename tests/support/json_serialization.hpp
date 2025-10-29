// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cinttypes>
#include <nlohmann/json.hpp>
#include <string_view>
#include <vector>

#include "json.hpp"

using namespace datadog::impl;

/**
 * Given a value of any JSON-serializable type T, serializes it with `EncodeJson` and
 * verifies that the resulting JSON is byte-for-byte identical to the expected value.
 */
template <typename T>
void RequireJsonLiteral(const T& value, std::string_view want) {
  std::vector<uint8_t> buffer;
  EncodeJson<T>(buffer, value);
  std::string_view got(reinterpret_cast<const char*>(buffer.data()), buffer.size());
  REQUIRE(got == want);
}

/**
 * Given a value of any JSON-serializable type T, serializes it with `EncodeJson`, then
 * parses the encoded value with nlohmann:json to verify that it's a valid JSON object,
 * then verifies that it is equivalent to the provided JSON object literal.
 */
template <typename T>
void RequireJsonObject(const T& value, std::string_view want_text) {
  auto want = nlohmann::json::parse(want_text);
  REQUIRE(want.is_object());

  std::vector<uint8_t> buffer;
  EncodeJson<T>(buffer, value);
  std::string_view text(reinterpret_cast<const char*>(buffer.data()), buffer.size());

  auto got = nlohmann::json::parse(text);
  REQUIRE(got == want);
}
