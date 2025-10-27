// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "json/primitives/null.hpp"

#include <charconv>
#include <cstring>

#include "assert.hpp"

namespace datadog::impl {

size_t WriteJson(char* dst, size_t n, const std::nullptr_t&) {
  DATADOG_ASSERT(n >= 4, "insufficient space for JSON null write");
  std::memcpy(dst, "null", 4);  // NOLINT(bugprone-not-null-terminated-result)
  return 4;
}

}  // namespace datadog::impl
