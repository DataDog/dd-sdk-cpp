// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "json/primitives/bool.hpp"

#include <charconv>

#include "assert.hpp"

namespace datadog::impl {

size_t GetJsonSize(const bool& value) { return value ? 4 : 5; }

size_t WriteJson(char* dst, size_t n, const bool& value) {
  if (value) {
    DATADOG_ASSERT(n >= 4, "insufficient space for JSON true write");
    std::memcpy(dst, "true", 4);  // NOLINT(bugprone-not-null-terminated-result)
    return 4;
  }
  DATADOG_ASSERT(n >= 5, "insufficient space for JSON false write");
  std::memcpy(dst, "false", 5);  // NOLINT(bugprone-not-null-terminated-result)
  return 5;
}

}  // namespace datadog::impl
