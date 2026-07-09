// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/util/validation.hpp"

#include <cctype>
#include <cmath>

#include "datadog/timestamp.hpp"

namespace datadog::impl {

bool IsBlankString(std::string_view s) {
  for (unsigned char c : s) {
    if (!std::isspace(c)) {
      return false;
    }
  }
  return true;
}

bool IsBlankCString(const char* s) { return s == nullptr || IsBlankString(s); }

bool IsValidLongTaskDurationSeconds(double duration_seconds) {
  if (!std::isfinite(duration_seconds) || duration_seconds <= 0) {
    return false;
  }
  constexpr double max_seconds = static_cast<double>(Duration::max().count()) *
                                 Duration::period::num / Duration::period::den;
  return duration_seconds <= max_seconds;
}

bool HasOnlyAllowedOperationNameCharacters(std::string_view s) {
  for (unsigned char c : s) {
    bool allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
                   c == '@' || c == '$';
    if (!allowed) {
      return false;
    }
  }
  return true;
}

}  // namespace datadog::impl
