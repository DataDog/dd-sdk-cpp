// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <string_view>
#include <type_traits>

#include "datadog/impl/core/http/body_writer.hpp"

namespace datadog::impl {

/**
 * Implementation of HttpBodyWriter that streams a string value into the HTTP request
 * body.
 */
struct StringWriter {
  std::string_view s;
  size_t offset{0};

  /**
   * Initializes a new StringWriter that will read the given string value and write it
   * into the request body when used as a functor.
   *
   * @param s The string data to write into the request body. The underlying storage
   *  for the string must remain stable throughout the lifetime of the StringWriter.
   */
  explicit StringWriter(std::string_view s) : s(s) {}

  /**
   * Function-call operator satisfying HttpBodyWriter:
   * - std::function<size_t(char* buffer, size_t num_bytes)>
   *
   * Writes the next available `num_bytes`-sized chunk of string data from `s` into
   * `buffer`, starting from the current `offset`, advancing `offset` in the process.
   */
  size_t operator()(char* buffer, size_t num_bytes);
};

static_assert(
    std::is_convertible_v<StringWriter, HttpBodyWriter>,
    "StringWriter does not implement HttpBodyWriter"
);

}  // namespace datadog::impl
