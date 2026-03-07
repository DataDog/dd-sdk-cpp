// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <catch2/catch_test_macros.hpp>
#include <initializer_list>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <vector>

#include "mock/http_client.hpp"

/**
 * Given any number of mock HTTP requests, parses the recorded requests' bodies,
 * assuming that each request body contains a valid JSON array, then returns a flattened
 * array of literal values collected from all arrays.
 */
inline nlohmann::json MergeJsonArrays(std::vector<MockHttpRequest>& requests) {
  // Collect a flattened array of JSON values from all requests
  nlohmann::json result = nlohmann::json::array();
  for (const auto& request : requests) {
    // If request was aborted, skip parsing
    if (request.aborted) {
      continue;
    }

    // Request body MUST be a JSON array
    auto arr = nlohmann::json::parse(request.body);
    REQUIRE(arr.is_array());

    // Extend result with all values from arr
    result.insert(result.end(), arr.begin(), arr.end());
  }
  return result;
}
