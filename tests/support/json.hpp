// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <catch2/catch_test_macros.hpp>
#include <initializer_list>
#include <sstream>
#include <string>
#include <vector>

#include "mock/http_client.hpp"

/**
 * Constructs a string representing the JSON array [<value-1>,<value-2>,...<value-N>].
 */
inline std::string JsonArrayOf(std::initializer_list<std::string> values) {
  std::ostringstream oss;
  oss << "[";
  int i = 0;
  for (const std::string& value : values) {
    if (i++ > 0) {
      oss << ',';
    }
    oss << value;
  }
  oss << "]";
  return oss.str();
}

/**
 * Given any number of mock HTTP requests, parses the recorded requests bodies, assuming
 * that each request body contains a valid JSON array, then returns a vector containing
 * the full list of literal values collected from all arrays.
 *
 * This is a bare-bones parsing function used for unit tests: it splits on ',' and it
 * can handle literal comma chars within quoted string values, but it doesn't handle
 * whitespace is otherwise very brittle.
 */
inline std::vector<std::string> ParseJsonArrays(
    std::vector<MockHttpRequest>& requests
) {
  // Collect the raw text of every JSON value from the body of each request
  std::vector<std::string> result;
  for (const auto& request : requests) {
    // Request body MUST be a JSON array literal with no padding
    const std::string& body = request.body;
    REQUIRE(body.size() >= 2);
    REQUIRE(body.front() == '[');
    REQUIRE(body.back() == ']');

    // Parse comma-separated values, handling quoted strings
    size_t pos = 1;
    const size_t end = body.size() - 1;
    size_t value_start = 1;
    std::string stack = "";
    while (pos < end) {
      // If we're inside a string literal, just scan forward until we find a closing
      // quote; don't do anything else
      const bool in_string_literal = !stack.empty() && stack.back() == '"';
      if (in_string_literal) {
        // Skip over escape sequences entirely
        if (body[pos] == '\\') {
          pos++;
          if (body[pos] == 'u') {
            pos++;
            pos++;
            pos++;
          }
          pos++;
          continue;
        }

        // If we see a non-escaped double-quote, it's the closing quote
        if (body[pos] == '"') {
          stack.pop_back();
        }
        pos++;
        continue;
      }

      // We're not in a string literal: if we see square brackets or curly braces,
      // update our stack to reflect current array/object nesting depth
      if (body[pos] == '[' || body[pos] == '{') {
        stack.push_back(body[pos]);
        pos++;
        continue;
      }

      if (body[pos] == ']' || body[pos] == '}') {
        const char counterpart = body[pos] == ']' ? '[' : '{';
        REQUIRE(stack.back() == counterpart);
        stack.pop_back();
        pos++;
        continue;
      }

      // If we're not nested in any sub-arrays or sub-objects, and the current character
      // is a comma, grab this value and chuck it into our result vector
      if (stack == "" && body[pos] == ',') {
        result.push_back(body.substr(value_start, pos - value_start));
        value_start = pos + 1;
      }
      pos++;
    }

    // Grab the final value in the array, if there is one
    if (pos > value_start) {
      result.push_back(body.substr(value_start, pos - value_start));
    }
  }
  return result;
}
